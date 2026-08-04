import scene_types;
import rt_types;
import brdf;
import base_color;
import emission;
import normal_map;
import material_fetch;

// Ray tracing closest-hit shader. Sole source for this shader. Emits
// SPIR-V only.
//
// Uses Slang T* pointers (PhysicalStorageBuffer / GL_EXT_buffer_reference2)
// to fetch vertices, indices, and materials via Vulkan buffer device
// addresses — the same mechanism as the GLSL buffer_reference. The shared
// BRDF math (import brdf) is used for the Epic/Unreal4 shading model.

[vk::binding(1, 0)] ConstantBuffer<SceneUBO> sceneUBO;

[vk::binding(4, 0)] SamplerState textureSamplers[MAX_TEXTURE_COUNT];
[vk::binding(3, 0)] Texture2D<float4> textures[MAX_TEXTURE_COUNT];

[vk::binding(0, 1)] RaytracingAccelerationStructure TLAS;

[[vk::push_constant]] PushConstantRaytracing pc_ray;

[shader("closesthit")]
void rchit_main(
    inout HitPayload payload,
    in BuiltInTriangleIntersectionAttributes attribs)
{
    // Per-mesh object description: instanceCustomIndex is the model's
    // first-mesh flat index; GeometryIndex is the mesh within the BLAS.
    uint objIndex = InstanceIndex() + GeometryIndex();
    ObjectDescription obj = fetch_object_description(objIndex);

    // Fetch geometry via buffer device addresses (T* = PhysicalStorageBuffer).
    Vertices* vertices = (Vertices*)obj.vertex_address;
    Indices* indices = (Indices*)obj.index_address;
    MaterialIDs* materialIDs = (MaterialIDs*)obj.material_index_address;
    Materials* materials = (Materials*)obj.material_address;

    // Indices of the closest-hit triangle.
    int3 ind = indices->i[PrimitiveIndex()];

    // Vertices of the closest-hit triangle.
    Vertex v0 = vertices->v[ind.x];
    Vertex v1 = vertices->v[ind.y];
    Vertex v2 = vertices->v[ind.z];

    // Barycentric coordinates of the intersection.
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
                                 attribs.barycentrics.x,
                                 attribs.barycentrics.y);

    // Hit position in object space -> world space. This is a column multiply
    // on purpose: ObjectToWorld() maps positions directly, unlike the normal
    // below.
    float3 hitPos = v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
    float3 worldHitPos = (float3)mul(ObjectToWorld(), float4(hitPos, 1.0));

    // Normal at hit position (inverse-transpose = row-multiply WorldToObject,
    // i.e. mul(v, M) not mul(M, v)).
    float3 normalHit = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
    float3 worldNormal = normalize(mul(normalHit, (float3x3)WorldToObject()));

    // Flip the normal so it points against the ray direction
    // (GLSL faceforward(N, I, N)).
    if (dot(worldNormal, WorldRayDirection()) > 0.0)
    {
        worldNormal = -worldNormal;
    }

    // Texture coordinates.
    float2 texCoords = v0.texture_coords * barycentrics.x
                     + v1.texture_coords * barycentrics.y
                     + v2.texture_coords * barycentrics.z;

    // Vertex colour at the hit position, same barycentric interpolation as
    // position/normal/texCoords above.
    float4 vertexColor = v0.color * barycentrics.x + v1.color * barycentrics.y + v2.color * barycentrics.z;

    // Material: per-primitive material ID.
    ObjMaterial material = materials->m[materialIDs->i[PrimitiveIndex()]];
    // Note: PrimitiveIndex() is valid here (closesthit stage).
    float3 albedo = float3(0.0);
    if (material.textureID >= 0)
    {
        int textureId = resolve_texture_slot(obj, material);
        // Explicit LOD: closesthit runs under RayClosestHitKHR, which has no
        // fragment derivatives, so an implicit-LOD .Sample() fails SPIR-V
        // validation (ImplicitLod requires Fragment/GLCompute/MeshEXT/TaskEXT).
        albedo += base_color(material, textures[textureId].SampleLevel(textureSamplers[textureId], transform_uv(texCoords, material), 0.0).xyz);
    }
    else
    {
        albedo += material.diffuse;
    }
    albedo *= vertexColor.rgb;

    // Light direction (towards the light) and view direction (towards camera).
    float3 L = normalize(-sceneUBO.dirLight.direction.xyz);
    float3 N = worldNormal;
    if (material.normalTextureID >= 0)
    {
        // Tangent at hit position, world-transformed. Unlike the normal above,
        // a tangent is a surface direction, so it transforms with
        // ObjectToWorld() directly (same rule as worldHitPos), not the
        // inverse-transpose WorldToObject() the normal uses.
        float4 tangentHit = v0.tangent * barycentrics.x + v1.tangent * barycentrics.y + v2.tangent * barycentrics.z;
        float3 worldTangent = normalize((float3)mul(ObjectToWorld(), float4(tangentHit.xyz, 0.0)));
        int normalTexId = resolve_texture_slot(obj, material.normalTextureID);
        // Explicit LOD: see the base-colour sample above for why.
        float3 normalSample = textures[normalTexId].SampleLevel(textureSamplers[normalTexId], transform_uv(texCoords, material), 0.0).xyz;
        N = apply_normal_map(N, float4(worldTangent, tangentHit.w), normalSample);
    }
    float3 V = normalize(sceneUBO.cam_pos.xyz - worldHitPos);

    // Shadow ray: trace from the hit point towards the light.
    bool shadowed = true;
    if (dot(worldNormal, L) > 0)
    {
        RayDesc shadowRay;
        shadowRay.Origin = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
        shadowRay.Direction = L;
        shadowRay.TMin = 0.001;
        shadowRay.TMax = 10000.0;

        TraceRay(TLAS,
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
            0xFFu,
            0u, 0u, 1u,   // sbt offset, stride, miss index (shadow miss)
            shadowRay,
            shadowed);
    }

    float roughness = material_roughness(material);
    float3 lightColor = sceneUBO.dirLight.color.rgb;
    float lightIntensity = sceneUBO.dirLight.color.w;

    // Forward, deferred lighting, and this closest-hit shader all start
    // shading an ObjMaterial from brdf_direct alone: this engine has no
    // ambient or IBL term, so seeding the payload with albedo here would
    // diverge from the other two paths.
    payload.hit_value = float3(0.0);
    payload.is_hit = 1.0;

    if (!shadowed)
    {
        float3 f0 = lerp(float3(0.04), albedo, material.metallic);
        payload.hit_value += brdf_direct(
            N, V, L, albedo, material.metallic, roughness, f0,
            lightColor * lightIntensity);
    }

    // Emissive is surface-emitted radiance, not reflected light - it is
    // unshadowed and unattenuated, so it is added after shadowing, not before.
    float3 emissiveSample = float3(1.0);
    if (material.emissiveTextureID >= 0)
    {
        int emissiveTexId = resolve_texture_slot(obj, material.emissiveTextureID);
        // Explicit LOD: see the base-colour sample above for why.
        emissiveSample = textures[emissiveTexId].SampleLevel(textureSamplers[emissiveTexId], transform_uv(texCoords, material), 0.0).xyz;
    }
    payload.hit_value += material_emission(material, emissiveSample);
}