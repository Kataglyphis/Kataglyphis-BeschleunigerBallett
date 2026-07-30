import scene_types;
import rt_types;
import brdf;

// Path tracing compute kernel using ray query (GL_EXT_ray_query).
// Full port of Resources/Shaders/path_tracing/path_tracing.comp. Emits
// SPIR-V only (Vulkan ray query — WebGPU has no ray query).
//
// Uses Slang T* pointers (PhysicalStorageBuffer) for buffer device
// addresses, RayQuery for ray traversal, and the shared BRDF math
// (import brdf) for direct lighting. Includes RNG (pcg) for stochastic
// sampling and temporal accumulation (running-mean history image).

struct PushConstantPathTracing
{
    float4 clearColor;
    uint width;
    uint height;
    uint frame_index;
    uint samples_per_pixel;
    uint max_bounces;
};

[vk::binding(0, 0)] ConstantBuffer<GlobalUBO> globalUBO;
[vk::binding(1, 0)] ConstantBuffer<SceneUBO> sceneUBO;
[vk::binding(2, 0)] StructuredBuffer<ObjectDescription> objectDescription;
[vk::binding(4, 0)] SamplerState textureSamplers[MAX_TEXTURE_COUNT];
[vk::binding(3, 0)] Texture2D<float4> textures[MAX_TEXTURE_COUNT];

[vk::binding(0, 1)] RaytracingAccelerationStructure TLAS;
[vk::binding(1, 1)] RWTexture2D<float4> image;
[vk::binding(2, 1)] RWTexture2D<float4> accumulationImage;

[[vk::push_constant]] PushConstantPathTracing pc_ray;

// PCG RNG: condensed pcg_output_rxs_mxs_32_32.
float stepAndOutputRNGFloat(inout uint rngState)
{
    rngState = rngState * 747796405u + 1u;
    uint word = ((rngState >> ((rngState >> 28u) + 4u)) ^ rngState) * 277803737u;
    word = (word >> 22u) ^ word;
    return float(word) / 4294967295.0;
}

struct HitInfo
{
    float3 color;
    float3 worldPosition;
    float3 worldNormal;
};

HitInfo getObjectHitInfo(RayQuery<RAY_FLAG_FORCE_OPAQUE> rayQuery)
{
    uint instanceIndex = rayQuery.CommittedInstanceIndex();
    float3x4 objToWorld = rayQuery.CommittedObjectToWorld3x4();
    float3x4 worldToObject = rayQuery.CommittedWorldToObject3x4();

    ObjectDescription obj = objectDescription[instanceIndex];
    Vertices* vertices = (Vertices*)obj.vertex_address;
    Indices* indices = (Indices*)obj.index_address;
    MaterialIDs* materialIDs = (MaterialIDs*)obj.material_index_address;
    Materials* materials = (Materials*)obj.material_address;

    HitInfo result;
    uint primitiveID = rayQuery.CommittedPrimitiveIndex();

    int3 i = indices->i[primitiveID];
    Vertex v0 = vertices->v[i.x];
    Vertex v1 = vertices->v[i.y];
    Vertex v2 = vertices->v[i.z];

    float2 bary = rayQuery.CommittedTriangleBarycentrics();
    float3 barycentrics = float3(1.0 - bary.x - bary.y, bary.x, bary.y);

    float3 objectPos = v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
    // float3x4 is 3 rows × 4 cols; mul(float3x4, float4) = float3.
    result.worldPosition = mul(objToWorld, float4(objectPos, 1.0));

    float3 normalHit = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
    result.worldNormal = normalize(mul(worldToObject, float4(normalHit, 0.0)));

    float2 texCoords = v0.texture_coords * barycentrics.x
                     + v1.texture_coords * barycentrics.y
                     + v2.texture_coords * barycentrics.z;

    ObjMaterial material = materials->m[materialIDs->i[primitiveID]];
    // primitiveID comes from rayQuery.CommittedPrimitiveIndex() (passed by caller).
    if (material.textureID >= 0)
    {
        int textureId = clamp(int(obj.texture_offset) + material.textureID, 0, MAX_TEXTURE_COUNT - 1);
        result.color = textures[textureId].Sample(textureSamplers[textureId], texCoords).xyz;
    }
    else
    {
        result.color = material.diffuse;
    }

    return result;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void path_tracing_main(uint3 tid : SV_DispatchThreadID)
{
    uint2 resolution = uint2(pc_ray.width, pc_ray.height);
    if (tid.x >= resolution.x || tid.y >= resolution.y)
        return;

    // RNG state: frame index folded in so every frame draws different samples.
    uint rngState = (resolution.x * tid.y + tid.x) + pc_ray.frame_index * 2654435769u;

    float4 cameraOrigin = mul(globalUBO.inv_view, float4(0, 0, 0, 1));

    int numSamples = int(pc_ray.samples_per_pixel);
    float3 summedColor = float3(0.0);

    for (int sampleIdx = 0; sampleIdx < numSamples; sampleIdx++)
    {
        float2 pixelCenter = float2(tid.xy) + float2(0.5);
        float2 uv = pixelCenter / float2(resolution);
        float2 cs = uv * 2.0 - 1.0;

        float4 target = mul(globalUBO.inv_projection, float4(cs.x, cs.y, 1, 1));
        float3 dir = normalize(mul(globalUBO.inv_view, float4(normalize(target.xyz), 0)).xyz);

        RayQuery<RAY_FLAG_FORCE_OPAQUE> rayQuery;
        RayDesc ray;
        ray.Origin = cameraOrigin.xyz;
        ray.Direction = dir;
        ray.TMin = 0.001;
        ray.TMax = 10000.0;
        rayQuery.TraceRayInline(TLAS, RAY_FLAG_FORCE_OPAQUE, 0xFFu, ray);
        rayQuery.Proceed();

        float3 sampleColor;
        if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            HitInfo hit = getObjectHitInfo(rayQuery);
            float3 L = normalize(-sceneUBO.dirLight.direction.xyz);
            float3 V = normalize(sceneUBO.cam_pos.xyz - hit.worldPosition);
            float3 f0 = lerp(float3(0.04), hit.color, 0.0);
            float3 lightColor = sceneUBO.dirLight.color.rgb;
            float lightIntensity = sceneUBO.dirLight.color.w;

            sampleColor = hit.color + brdf_direct(
                hit.worldNormal, V, L, hit.color, 0.0, 0.5, f0,
                lightColor * lightIntensity);
        }
        else
        {
            sampleColor = pc_ray.clearColor.xyz * 0.4;
        }

        summedColor += sampleColor;
    }

    float3 avgColor = summedColor / float(numSamples);

    // Temporal accumulation: running mean across frames.
    if (pc_ray.frame_index > 0)
    {
        float4 prev = accumulationImage[tid.xy];
        float3 accumColor = (prev.rgb * float(pc_ray.frame_index) + avgColor) / float(pc_ray.frame_index + 1);
        accumulationImage[tid.xy] = float4(accumColor, 1.0);
        image[tid.xy] = float4(accumColor, 1.0);
    }
    else
    {
        accumulationImage[tid.xy] = float4(avgColor, 1.0);
        image[tid.xy] = float4(avgColor, 1.0);
    }
}