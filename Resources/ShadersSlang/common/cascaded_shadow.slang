import scene_types;

// Cascaded shadow sampling shared by the forward and deferred lighting
// shaders. Sole source for this shader. Uses scene_types (NOT rt_types) to
// avoid pulling ray tracing capabilities into rasterization shaders.

// Comparison sampler (bound once, VkSampler compareEnable/compareOp set in
// CascadedShadowMap.cpp): SampleCmpLevelZero below gets a free bilinear 2x2
// PCF tap per sample instead of thresholding a bilinear-filtered raw depth
// value, which is not PCF (it blends depths, not visibility).
[vk::binding(5, 0)] Sampler2DArrayShadow directionalShadowMaps;

// Fraction of direct light blocked: 0 = fully lit, 1 = fully shadowed.
float calc_cascaded_shadow(float3 worldPos, float3 N, float3 L, ConstantBuffer<SceneUBO> sceneUBO)
{
    int cascadeCount = int(sceneUBO.numCascades);
    if (cascadeCount <= 0) return 0.0;

    float fragDistance = dot(worldPos - sceneUBO.cam_pos.xyz, normalize(sceneUBO.view_dir.xyz));
    int cascadeIndex = cascadeCount - 1;
    for (int i = 0; i < cascadeCount; i++)
    {
        if (fragDistance < sceneUBO.cascadeSplits[i])
        {
            cascadeIndex = i;
            break;
        }
    }

    float4 lightSpacePos = mul(sceneUBO.cascadeLightSpaceMatrices[cascadeIndex], float4(worldPos, 1.0));
    float3 proj = lightSpacePos.xyz / lightSpacePos.w;
    float2 shadowUV = proj.xy * 0.5 + 0.5;
    float currentDepth = proj.z;
    if (currentDepth > 1.0 || currentDepth < 0.0 ||
        shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0)
        return 0.0;

    float bias = max(0.002 * (1.0 - dot(N, L)), 0.0005);
    int radius = int(sceneUBO.pcfRadius);
    uint shadowWidth, shadowHeight, shadowLayers;
    directionalShadowMaps.GetDimensions(shadowWidth, shadowHeight, shadowLayers);
    float2 texelSize = 1.0 / float2(float(shadowWidth), float(shadowHeight));
    float visible = 0.0;
    float taps = 0.0;
    // The comparison sampler is ClampToEdge (CascadedShadowMap.cpp), so a tap
    // that wanders past the map's border re-samples the border texel's depth
    // instead of falling off the map - it does NOT read as unshadowed.
    // ClampToBorder + an opaque-white border would do that instead, but is
    // blocked here: Texture.cpp:249 hard-codes an int opaque-black border
    // colour, which reads as depth 0 (fully shadowed) on this float-format
    // image. This in-shader bounds check is the portable equivalent: an
    // out-of-map tap counts as lit, matching the whole-fragment rejection
    // above.
    for (int x = -radius; x <= radius; x++)
    {
        for (int y = -radius; y <= radius; y++)
        {
            float2 uv = shadowUV + float2(x, y) * texelSize;
            visible += (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0)
              ? directionalShadowMaps.SampleCmpLevelZero(float3(uv, float(cascadeIndex)), currentDepth - bias)
              : 1.0;
            taps += 1.0;
        }
    }
    return 1.0 - visible / max(taps, 1.0);
}