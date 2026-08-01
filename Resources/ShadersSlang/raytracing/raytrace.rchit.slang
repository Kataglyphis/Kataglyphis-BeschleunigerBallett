import scene_types;
import rt_types;
import brdf;

// Ray tracing closest-hit shader. Sole source for this shader. Emits
// SPIR-V only.
//
// Uses Slang T* pointers (PhysicalStorageBuffer / GL_EXT_buffer_reference2)
// to fetch vertices, indices, and materials via Vulkan buffer device
// addresses — the same mechanism as the GLSL buffer_reference. The shared
// BRDF math (import brdf) is used for the Epic/Unreal4 shading model.

[vk::binding(1, 0)] ConstantBuffer<SceneUBO> sceneUBO;

[vk::binding(2, 0)] StructuredBuffer<ObjectDescription> objectDescription;

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
    ObjectDescription obj = objectDescription[objIndex];

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

    // Hit position in object space -> world space.
    float3 hitPos = v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
    float3 worldHitPos = (float3)mul(ObjectToWorld(), float4(hitPos, 1.0));

    // Normal at hit position (inverse-transpose = row-multiply WorldToObject).
    float3 normalHit = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
    float3 worldNormal = normalize((float3)mul((float3x3)WorldToObject(), normalHit));

    // Texture coordinates.
    float2 texCoords = v0.texture_coords * barycentrics.x
                     + v1.texture_coords * barycentrics.y
                     + v2.texture_coords * barycentrics.z;

    // Material: per-primitive material ID.
    ObjMaterial material = materials->m[materialIDs->i[PrimitiveIndex()]];
    // Note: PrimitiveIndex() is valid here (closesthit stage).
    float3 ambient = float3(0.0);
    if (material.textureID >= 0)
    {
        int textureId = clamp(int(obj.texture_offset) + material.textureID, 0, MAX_TEXTURE_COUNT - 1);
        // Explicit LOD: closesthit runs under RayClosestHitKHR, which has no
        // fragment derivatives, so an implicit-LOD .Sample() fails SPIR-V
        // validation (ImplicitLod requires Fragment/GLCompute/MeshEXT/TaskEXT).
        ambient += textures[textureId].SampleLevel(textureSamplers[textureId], texCoords, 0.0).xyz;
    }
    else
    {
        ambient += material.diffuse;
    }

    // Light direction (towards the light) and view direction (towards camera).
    float3 L = normalize(-sceneUBO.dirLight.direction.xyz);
    float3 N = worldNormal;
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
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_OPAQUE,
            0xFFu,
            0u, 0u, 1u,   // sbt offset, stride, miss index (shadow miss)
            shadowRay,
            shadowed);
    }

    float roughness = 0.9;
    float3 lightColor = sceneUBO.dirLight.color.rgb;
    float lightIntensity = sceneUBO.dirLight.color.w;

    payload.hit_value = ambient;
    payload.is_hit = 1.0;

    if (!shadowed)
    {
        float3 f0 = lerp(float3(0.04), ambient, 0.0);
        payload.hit_value += brdf_direct(
            N, V, L, ambient, 0.0, roughness, f0,
            lightColor * lightIntensity);
    }
}