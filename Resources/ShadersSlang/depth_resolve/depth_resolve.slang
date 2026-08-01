import fullscreen;

// Depth resolve: downsamples MSAA depth to single-sample via nearest-sample
// selection (minimum depth = nearest fragment). Mirrors depth_resolve.wgsl.
// Emits WGSL.

[vk::binding(0, 0)] Texture2DMS<float> msaaDepth;

[shader("vertex")]
FullscreenVsOut vs_main(uint vid : SV_VertexID)
{
    // Was a hand-rolled fullscreen triangle whose uv grew WITH ndc.y (uv.y=0
    // at the bottom, 1 at the top) instead of matching the standard
    // framebuffer/@builtin(position) convention fullscreen_vs uses (uv.y=0 at
    // the top). fs_main below reads msaaDepth at that uv but WRITES via
    // SV_Depth at its own (correctly, hardware-placed) fragCoord, so every
    // resolved depth texel held the wrong source row - self.depth came out
    // vertically mirrored versus the actual screen for any asymmetric scene,
    // silently correct only when a consumer's own geometry was symmetric
    // about the horizontal centreline (gpu_cull, ssao's own callers).
    return fullscreen_vs(vid);
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