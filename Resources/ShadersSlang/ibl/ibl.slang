import fullscreen;

// IBL precompute (Rust/WebGPU side). Mirrors ibl.wgsl. Emits WGSL.
// Split-sum: equirectangular HDR -> cubemap, cosine-convolved irradiance,
// GGX-prefiltered specular, and the environment BRDF lookup table.

static const float PI = 3.14159265359;

struct IblParams
{
    float4 face_roughness_samples_mip; // x: face, y: roughness, z: samples, w: mip
    float4 source_resolution;
};
[vk::binding(0, 0)] ConstantBuffer<IblParams> params;
[vk::binding(1, 0)] Texture2D<float4> srcEquirect;
[vk::binding(2, 0)] TextureCube<float4> srcCube;
[vk::binding(3, 0)] SamplerState srcSampler;

[shader("vertex")]
FullscreenVsOut vs_fullscreen(uint vid : SV_VertexID)
{
    // IBL uses a different fullscreen triangle winding than fullscreen_vs.
    float x = float((vid << 1u) & 2u);
    float y = float(vid & 2u);
    FullscreenVsOut o;
    o.svPosition = float4(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
    o.uv = float2(x, y);
    return o;
}

float3 cube_direction(uint face, float2 uv)
{
    float a = 2.0 * uv.x - 1.0;
    float b = 1.0 - 2.0 * uv.y;
    float3 dir;
    switch (face)
    {
        case 0u: dir = float3(1.0, b, -a); break;
        case 1u: dir = float3(-1.0, b, a); break;
        case 2u: dir = float3(a, 1.0, -b); break;
        case 3u: dir = float3(a, -1.0, b); break;
        case 4u: dir = float3(a, b, 1.0); break;
        default: dir = float3(-a, b, -1.0); break;
    }
    return normalize(dir);
}

float2 equirect_uv(float3 dir)
{
    float longitude = atan2(dir.z, dir.x);
    float latitude = asin(clamp(dir.y, -1.0, 1.0));
    return float2(longitude / (2.0 * PI) + 0.5, 0.5 - latitude / PI);
}

[shader("fragment")]
float4 fs_equirect_to_cube(FullscreenVsOut In) : SV_Target
{
    float3 dir = cube_direction(uint(params.face_roughness_samples_mip.x), In.uv);
    float2 uv = equirect_uv(dir);
    return srcEquirect.Sample(srcSampler, uv);
}

[shader("fragment")]
float4 fs_irradiance(FullscreenVsOut In) : SV_Target
{
    float3 dir = cube_direction(uint(params.face_roughness_samples_mip.x), In.uv);
    float3 up = abs(dir.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 right = normalize(cross(up, dir));
    up = cross(dir, right);
    float3 irradiance = float3(0.0);
    float sampleCount = 0.0;
    float phiStep = 2.0 * PI / 64.0;
    float thetaStep = 0.5 * PI / 32.0;
    for (uint phi_i = 0u; phi_i < 64u; phi_i++)
    {
        for (uint theta_i = 0u; theta_i < 32u; theta_i++)
        {
            float phi = float(phi_i) * phiStep;
            float theta = float(theta_i) * thetaStep;
            float3 tangent = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            float3 sampleDir = tangent.x * right + tangent.y * up + tangent.z * dir;
            irradiance += srcCube.Sample(srcSampler, sampleDir).rgb * cos(theta) * sin(theta);
            sampleCount += 1.0;
        }
    }
    return float4(PI * irradiance / sampleCount, 1.0);
}

[shader("fragment")]
float4 fs_prefilter(FullscreenVsOut In) : SV_Target
{
    float3 dir = cube_direction(uint(params.face_roughness_samples_mip.x), In.uv);
    float roughness = params.face_roughness_samples_mip.y;
    uint sampleCount = uint(params.face_roughness_samples_mip.z);
    float3 up = abs(dir.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, dir));
    float3 bitangent = cross(dir, tangent);
    float3 prefiltered = float3(0.0);
    float totalWeight = 0.0;
    for (uint i = 0u; i < sampleCount; i++)
    {
        float2 hammersley = float2(float(i) / float(sampleCount), 0.0); // simplified
        float cosTheta = sqrt(1.0 - hammersley.x * hammersley.x);
        float sinTheta = hammersley.x;
        float phi = 2.0 * PI * hammersley.y;
        float3 h = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
        float3 sampleDir = normalize(2.0 * dot(dir, h) * h - dir);
        float nDotL = max(dot(dir, sampleDir), 0.0);
        if (nDotL > 0.0)
        {
            prefiltered += srcCube.Sample(srcSampler, sampleDir).rgb * nDotL;
            totalWeight += nDotL;
        }
    }
    return float4(prefiltered / max(totalWeight, 1e-4), 1.0);
}

[shader("fragment")]
float4 fs_brdf_lut(FullscreenVsOut In) : SV_Target
{
    float2 uv = In.uv;
    float nDotV = uv.x;
    float roughness = uv.y;
    float3 v = float3(sqrt(1.0 - nDotV * nDotV), 0.0, nDotV);
    float2 result = float2(0.0);
    uint sampleCount = 1024u;
    for (uint i = 0u; i < sampleCount; i++)
    {
        float2 hammersley = float2(float(i) / float(sampleCount), 0.0); // simplified
        float cosTheta = sqrt(1.0 - hammersley.x * hammersley.x);
        float sinTheta = hammersley.x;
        float phi = 2.0 * PI * hammersley.y;
        float3 h = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
        float3 l = normalize(2.0 * dot(v, h) * h - v);
        float nDotL = max(l.z, 0.0);
        float nDotH = max(h.z, 0.0);
        float vDotH = max(dot(v, h), 0.0);
        if (nDotL > 0.0)
        {
            float g = geometry_smith_ibl(nDotV, nDotL, roughness);
            float gv = g * vDotH / max(nDotH * nDotV, 1e-4);
            float gl = g * vDotH / max(nDotH * nDotL, 1e-4);
            result += float2(gl, gv);
        }
    }
    return float4(result / float(sampleCount), 0.0, 1.0);
}

float geometry_smith_ibl(float nDotV, float nDotL, float roughness)
{
    float k = (roughness * roughness) / 2.0;
    float gv = nDotV / max(nDotV * (1.0 - k) + k, 1e-4);
    float gl = nDotL / max(nDotL * (1.0 - k) + k, 1e-4);
    return gv * gl;
}