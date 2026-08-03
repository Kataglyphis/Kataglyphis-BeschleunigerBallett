// GPU occlusion culling detection (Rust/WebGPU side). occlusion_bbox.wgsl
// is generated from this file and checked into the Rust crate by wgslMap.
// Emits WGSL. Draws a primitive's world-space AABB as a solid box for
// hardware occlusion queries.

struct OcclusionUniforms
{
    float4x4 view_proj;
};
[vk::binding(0, 0)] ConstantBuffer<OcclusionUniforms> uniforms;

struct BboxInstance
{
    float3 aabb_min : POSITION;
    float3 aabb_max : INSTANCE0;
};

static const uint corners[36] = {
    0u, 2u, 1u, 1u, 2u, 3u,  // -Z
    4u, 5u, 6u, 5u, 7u, 6u,  // +Z
    0u, 1u, 4u, 1u, 5u, 4u,  // -Y
    2u, 6u, 3u, 3u, 6u, 7u,  // +Y
    0u, 4u, 2u, 2u, 4u, 6u,  // -X
    1u, 3u, 5u, 3u, 7u, 5u,  // +X
};

[shader("vertex")]
float4 vs_main(uint vertexIndex : SV_VertexID, BboxInstance instance) : SV_Position
{
    uint c = corners[vertexIndex];
    float3 sel = float3(float(c & 1u), float((c >> 1u) & 1u), float((c >> 2u) & 1u));
    // Expand the box by a small margin before testing. A primitive whose AABB
    // coincides with its own front face (an axis-aligned box is its own AABB)
    // otherwise z-fights the stored depth under LessEqual, and only a fragment
    // or two survive - a knife-edge that reads as visible on one GPU and
    // occluded on another. The margin pulls the box's front slightly toward the
    // camera so a genuinely visible primitive passes robustly. This is the
    // conservative direction: over-reporting visibility never wrongly skips a
    // draw, and the margin (2% of the half-extent plus 1 cm) is far smaller
    // than the depth gap a real occluder puts behind it, so it cannot make an
    // occluded primitive test visible. Mirrored on the CPU side by
    // `aabb_contains_point` (forward.rs), which must agree with this margin.
    float3 half3 = (instance.aabb_max - instance.aabb_min) * 0.5;
    float3 margin = half3 * 0.02 + float3(0.01);
    float3 expandedMin = instance.aabb_min - margin;
    float3 expandedMax = instance.aabb_max + margin;
    float3 pos = lerp(expandedMin, expandedMax, sel);
    return mul(uniforms.view_proj, float4(pos, 1.0));
}

// No color output: the pass has zero color attachments and only the depth test
// and occlusion count matter. WebGPU still wants a fragment stage to complete
// the pipeline, so this is an empty entry point.
[shader("fragment")]
void fs_main()
{
}