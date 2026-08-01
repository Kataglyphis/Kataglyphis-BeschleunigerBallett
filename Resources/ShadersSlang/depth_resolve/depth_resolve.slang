import fullscreen;

// Depth resolve: downsamples MSAA depth to single-sample via nearest-sample
// selection (minimum depth = nearest fragment). Mirrors depth_resolve.wgsl.
// Emits WGSL.

[vk::binding(0, 0)] Texture2DMS<float> msaaDepth;

[shader("vertex")]
FullscreenVsOut vs_main(uint vid : SV_VertexID)
{
    FullscreenVsOut o;
    float2 uv = float2(float(vid & 1u) * 2.0, float((vid >> 1u) & 1u) * 2.0);
    o.svPosition = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    o.uv = uv;
    return o;
}

[shader("fragment")]
float fs_main(FullscreenVsOut In) : SV_Depth
{
    uint w, h, samples;
    msaaDepth.GetDimensions(w, h, samples);
    int2 coords = int2(In.uv * float2(float(w), float(h)));
    float minDepth = 1.0;
    for (uint i = 0u; i < samples; i++)
    {
        float d = msaaDepth.Load(int2(coords), i);
        minDepth = min(minDepth, d);
    }
    return minDepth;
}