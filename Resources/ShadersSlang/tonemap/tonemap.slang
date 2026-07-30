import aces;
import fullscreen;

// Fullscreen tonemap pass (Rust/WebGPU side). Mirrors tonemap.wgsl; the
// shared ACES + sRGB-encode math comes from aces.slang (imported), and the
// fullscreen-triangle vertex from fullscreen.slang. Bindings match the
// Rust renderer's bind group 0 layout (see forward.rs TonemapPass).
//
// This is the first Slang entry point: it proves `import aces` compiles to
// WGSL with correct binding translation. The C++ side has its own
// post.slang (tonemap + clouds) because the two renderers composite
// differently — see docs/shader-sharing.md.

[vk::binding(0, 0)] Texture2D<float4> hdrTex;
[vk::binding(1, 0)] SamplerState hdrSampler;
[vk::binding(2, 0)] Texture2D<float4> bloomTex;
[vk::binding(4, 0)] Texture2D<float4> aoTex;

struct TonemapUniforms
{
    // x: bloom strength, y: SSAO strength, z: unused, w: 1.0 when this
    // shader must gamma-encode its own output (non-sRGB web canvas).
    float4 params;
};
[vk::binding(3, 0)] ConstantBuffer<TonemapUniforms> tonemapUniforms;

// [adapted EV, target EV], written by cs_reduce_exposure. Read here rather
// than passed through params.z so there is ONE source of truth.
[vk::binding(5, 0)] StructuredBuffer<float> exposureState;

// The vertex output type matches FullscreenVsOut from fullscreen.slang;
// declared here so the fragment stage can name it without importing.
struct VsOut
{
    float4 svPosition : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VsOut vs_main(uint vid : SV_VertexID)
{
    // Shared fullscreen-triangle vertex (no vertex buffer). The import
    // removes the duplicated x/y/uv math that was copy-pasted in every
    // fullscreen pass on both renderers.
    FullscreenVsOut o = fullscreen_vs(vid);
    VsOut r;
    r.svPosition = o.svPosition;
    r.uv = o.uv;
    return r;
}

[shader("fragment")]
float4 fs_main(VsOut In) : SV_Target
{
    float3 hdr = hdrTex.Sample(hdrSampler, In.uv).rgb;
    float3 bloom = bloomTex.Sample(hdrSampler, In.uv).rgb;
    float aoRaw = aoTex.Sample(hdrSampler, In.uv).r;
    float ao = lerp(1.0, aoRaw, tonemapUniforms.params.y);
    float exposure = exp2(exposureState[0]);
    float3 mapped = aces_tonemap((hdr * ao + bloom * tonemapUniforms.params.x) * exposure);

    // An sRGB target encodes in hardware and gets linear values. WebGPU
    // canvases expose no sRGB surface format, so on web the encode happens
    // here (see docs/webgpu-srgb-audit.md).
    if (tonemapUniforms.params.w > 0.5)
        return float4(linear_to_srgb(mapped), 1.0);
    return float4(mapped, 1.0);
}