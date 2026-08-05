import aces;
import fullscreen;

// C++/Vulkan fullscreen post pass: tonemap + optional cloud compositing.
// Sole source for this shader; uses the shared ACES math (imported) and the
// exact sRGB encode instead of pow(x, 1/2.2). Push constants carry the
// feature toggles — see
// Src/GraphicsEngineVulkan/renderer/pushConstants/PushConstantPost.hpp.
// The fullscreen-triangle vertex comes from fullscreen.slang (imported).
//
// Emits SPIR-V only (the C++ renderer loads .spv). The Rust renderer has its
// own tonemap.slang (different compositing: bloom + SSAO + exposure).

// Combined image+sampler (Vulkan combined image sampler descriptor).
[vk::binding(0, 0)] Sampler2D noisyTxt;
[vk::binding(1, 0)] Sampler2D cloudTxt;

struct PushConstantPost
{
    uint clouds_enabled;
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
    float4 sampled = noisyTxt.Sample(In.uv);
    float3 color = sampled.rgb;
    float  alpha = sampled.a;

    if (bool(pc_post.clouds_enabled))
    {
        float4 cloud = cloudTxt.Sample(In.uv);
        color = cloud.rgb + color * (1.0 - cloud.a);
        alpha = cloud.a + alpha * (1.0 - cloud.a);
    }

    // ACES filmic tonemap (shared with Rust WebGPU renderer via aces.slang).
    float3 tonemapped = aces_tonemap(color);

    // The swapchain is UNORM, not sRGB (SwapchainChoices.hpp's
    // chooseBestSurfaceFormat prefers eR8G8B8A8Unorm/eB8G8R8A8Unorm with an
    // eSrgbNonlinear color space), so there is no hardware encode on
    // present - the shader must do it itself, exactly like tonemap.slang's
    // non-sRGB-canvas path does on the web renderer.
    return float4(linear_to_srgb(tonemapped), alpha);
}