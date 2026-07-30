import aces;
import fullscreen;

// C++/Vulkan fullscreen post pass: tonemap + optional cloud compositing.
// Mirrors Resources/Shaders/post/post.frag but uses the shared ACES math
// (imported) instead of the hand-written generated/aces.glsl, and the exact
// sRGB encode instead of pow(x, 1/2.2). Push constants carry the feature
// toggles — see Src/GraphicsEngineVulkan/renderer/pushConstants/PushConstantPost.hpp.
// The fullscreen-triangle vertex comes from fullscreen.slang (imported).
//
// Emits SPIR-V only (the C++ renderer loads .spv). The Rust renderer has its
// own tonemap.slang (different compositing: bloom + SSAO + exposure).

// Combined image+sampler (maps to GLSL sampler2D / Vulkan combined
// descriptor). Matches the C++ post.frag's `uniform sampler2D` layout.
[vk::binding(0, 0)] Sampler2D noisyTxt;
[vk::binding(1, 0)] Sampler2D cloudTxt;

struct PushConstantPost
{
    float aspect_ratio;
    uint  clouds_enabled;
    uint  shadows_enabled;
    uint  skybox_enabled;
};
[[vk::push_constant]] PushConstantPost pc_post;

struct VsOut
{
    float4 svPosition : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VsOut vs_main(uint vid : SV_VertexID)
{
    // Shared fullscreen-triangle vertex (no vertex buffer).
    FullscreenVsOut o = fullscreen_vs(vid);
    VsOut r;
    r.svPosition = o.svPosition;
    r.uv = o.uv;
    return r;
}

[shader("fragment")]
float4 fs_main(VsOut In) : SV_Target
{
    float3 color = noisyTxt.Sample(In.uv).rgb;
    float  alpha = noisyTxt.Sample(In.uv).a;

    if (bool(pc_post.clouds_enabled))
    {
        float4 cloud = cloudTxt.Sample(In.uv);
        color = cloud.rgb + color * (1.0 - cloud.a);
        alpha = cloud.a + alpha * (1.0 - cloud.a);
    }

    // ACES filmic tonemap (shared with Rust WebGPU renderer via aces.slang).
    float3 tonemapped = aces_tonemap(color);

    // The C++ engine renders to an sRGB target, so the hardware encodes and
    // we emit linear values. (The Rust side conditionally encodes itself for
    // non-sRGB web canvases — see tonemap.slang.)
    return float4(tonemapped, alpha);
}