// Procedural sky (Rust/WebGPU side). Mirrors sky.wgsl. Emits WGSL.
// Horizon/zenith gradient + analytic sun disk derived from the directional light.

struct SkyUniforms
{
    float4x4 inv_view_proj;
    float4 light_dir_intensity; // xyz: dir towards light, w: intensity
};
[vk::binding(0, 0)] ConstantBuffer<SkyUniforms> sky;

struct SkyVsOut
{
    float4 svPosition : SV_Position;
    float2 ndc : TEXCOORD0;
};

[shader("vertex")]
SkyVsOut vs_main(uint vid : SV_VertexID)
{
    SkyVsOut o;
    float x = float(vid / 2) * 4.0 - 1.0;
    float y = float(vid % 2) * 4.0 - 1.0;
    o.svPosition = float4(x, y, 1.0, 1.0);
    o.ndc = float2(x, y);
    return o;
}

[shader("fragment")]
float4 fs_main(SkyVsOut In) : SV_Target
{
    float4 nearP = mul(sky.inv_view_proj, float4(In.ndc, 0.0, 1.0));
    float4 farP = mul(sky.inv_view_proj, float4(In.ndc, 1.0, 1.0));
    float3 dir = normalize(farP.xyz / farP.w - nearP.xyz / nearP.w);

    float3 zenith = float3(0.09, 0.16, 0.35);
    float3 horizon = float3(0.55, 0.62, 0.72);
    float3 ground = float3(0.18, 0.16, 0.15);

    float3 color;
    if (dir.y >= 0.0)
        color = lerp(horizon, zenith, pow(clamp(dir.y, 0.0, 1.0), 0.7));
    else
        color = lerp(horizon, ground, clamp(-dir.y * 3.0, 0.0, 1.0));

    float3 l = normalize(sky.light_dir_intensity.xyz);
    float cosSun = max(dot(dir, l), 0.0);
    float sun = pow(cosSun, 1200.0) * 24.0 + pow(cosSun, 48.0) * 0.5;
    color += float3(1.0, 0.95, 0.85) * sun * (sky.light_dir_intensity.w * 0.4);

    return float4(color, 1.0);
}