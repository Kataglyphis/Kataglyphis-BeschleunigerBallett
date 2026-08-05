// Shared color math: ACES filmic tonemap + exact sRGB encode.
//
// Single source of truth for both renderers:
//   - C++/Vulkan consumes the SPIR-V emit (imported directly, no GLSL
//     intermediary).
//   - Rust/WebGPU consumes the WGSL emit.
//
// ACES is the Narkowicz 2015 approximation (matches the Rust renderer's
// existing `aces()` in tonemap.wgsl). linear_to_srgb is the exact IEC
// 61966-2-1 transfer function (NOT pow(x, 1/2.2)) — see
// docs/webgpu-srgb-audit.md: the approximation is visibly wrong in the
// darks. Both post/post.slang and tonemap/tonemap.slang import this module;
// post.slang's fs_main calls aces_tonemap.

// ACES filmic tonemap (Narkowicz 2015).
float3 aces_tonemap(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), float3(0.0), float3(1.0));
}

// Exact IEC 61966-2-1 sRGB transfer function (linear -> sRGB encoded).
// Matches what an sRGB render target's hardware encode does, so a shader
// that must encode itself (e.g. a non-sRGB web canvas) gets the same curve
// the hardware would apply.
float3 linear_to_srgb(float3 c)
{
    float3 low = c * 12.92;
    float3 high = 1.055 * pow(max(c, float3(0.0)), float3(1.0 / 2.4)) - 0.055;
    return select(c < float3(0.0031308), low, high);
}