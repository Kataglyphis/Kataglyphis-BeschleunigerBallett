// Luminance histogram for auto-exposure (Rust/WebGPU side). Mirrors
// histogram.wgsl. Emits WGSL.

static const uint HISTOGRAM_BINS = 64u;
static const float MIN_LOG_LUMINANCE = -10.0;
static const float MAX_LOG_LUMINANCE = 4.0;
static const float BLACK_THRESHOLD = 1e-6;

[vk::binding(0, 0)] Texture2D<float4> hdrTex;
[vk::binding(1, 0)] RWStructuredBuffer<uint> histogram;

float luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

uint histogram_bin(float lum)
{
    if (!(lum > BLACK_THRESHOLD)) return 0u;
    float logLum = log2(lum);
    float normalized = clamp((logLum - MIN_LOG_LUMINANCE) / (MAX_LOG_LUMINANCE - MIN_LOG_LUMINANCE), 0.0, 1.0);
    float span = float(HISTOGRAM_BINS - 2u);
    return 1u + min(uint(normalized * span), HISTOGRAM_BINS - 2u);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void histogram_main(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    hdrTex.GetDimensions(w, h);
    if (id.x < w && id.y < h)
    {
        float3 color = hdrTex.Load(int3(id.xy, 0)).rgb;
        uint bin = histogram_bin(luminance(color));
        // Direct global atomic add (WGSL-compatible; the groupshared
        // privatization was dropped because InterlockedExchange on
        // groupshared arrays is not available for the WGSL target).
        InterlockedAdd(histogram[bin], 1u);
    }
}