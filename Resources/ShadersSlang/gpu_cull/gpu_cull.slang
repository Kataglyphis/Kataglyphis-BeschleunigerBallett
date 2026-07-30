// GPU occlusion culling (Rust/WebGPU side). Mirrors gpu_cull.wgsl. Emits WGSL.
// Tests each primitive's projected AABB against the depth buffer.

struct CullParams
{
    float4x4 view_proj;
    float4x4 inv_view_proj;
    float width;
    float height;
    uint primitive_count;
};
[vk::binding(0, 0)] Texture2D<float> depthTex;
[vk::binding(1, 0)] ConstantBuffer<CullParams> params;

struct Aabb
{
    float4 min;
    float4 max;
};
[vk::binding(2, 0)] StructuredBuffer<Aabb> aabbData;
[vk::binding(3, 0)] RWStructuredBuffer<uint> visibility;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 gid : SV_DispatchThreadID)
{
    uint idx = gid.x;
    if (idx >= params.primitive_count) return;

    Aabb aabb = aabbData[idx];
    float3 center = (aabb.min.xyz + aabb.max.xyz) * 0.5;
    float4 clip = mul(params.view_proj, float4(center, 1.0));
    float2 ndc = clip.xy / clip.w;
    float depth = clip.z / clip.w;
    float2 uv = ndc * 0.5 + 0.5;

    if (depth < 0.0 || depth > 1.0) { visibility[idx] = 0u; return; }
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) { visibility[idx] = 0u; return; }

    uint w, h;
    depthTex.GetDimensions(w, h);
    int2 coords = int2(uv * float2(float(w), float(h)));
    float sampledDepth = depthTex.Load(int3(coords, 0));
    visibility[idx] = (depth <= sampledDepth) ? 1u : 0u;
}