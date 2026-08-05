import scene_types;
import push_constants;

// Shared vertex stage for the forward rasterizer (rasterizer.slang's
// vs_main) and the deferred geometry pass (deferred.slang's
// geometry_vs_main). The forward/deferred parity golden compares their
// outputs pixel-for-pixel, so the two passes must produce identical world
// position, normal and tangent by construction - by sharing one function -
// rather than by keeping two hand-written copies in sync through review.

struct RasterVsOut
{
    float4 svPosition : SV_Position;
    float2 texCoords : TEXCOORD0;
    float3 shadingNormal : TEXCOORD1;
    float4 fragmentColor : TEXCOORD2;
    float3 worldPosition : TEXCOORD3;
    float4 worldTangent : TEXCOORD4;
};

RasterVsOut raster_geometry_vs(
    ConstantBuffer<GlobalUBO> globalUBO,
    PushConstantRasterizer pc,
    float3 position,
    float3 normal,
    float4 color,
    float2 texCoords,
    float4 tangent)
{
    RasterVsOut o;
    o.svPosition = mul(mul(mul(globalUBO.projection, globalUBO.view), pc.model), float4(position, 1.0));
    o.worldPosition = mul(pc.model, float4(position, 1.0)).xyz;
    o.shadingNormal = transform_normal(pc, normal);
    // Tangent is a surface direction, not a normal, so it transforms with the
    // model matrix directly rather than the inverse-transpose transform_normal()
    // uses (see push_constants.slang's transform_normal doc).
    o.worldTangent = float4(normalize(mul(pc.model, float4(tangent.xyz, 0.0)).xyz), tangent.w);
    o.texCoords = texCoords;
    o.fragmentColor = color;
    return o;
}
