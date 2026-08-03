import brdf;

// Forward PBR pass (Rust/WebGPU side). Mirrors forward.wgsl. Emits WGSL.
// Uses the shared BRDF math (import brdf), eliminating the duplicated BRDF
// functions that were in forward.wgsl. ACES tonemapping happens later, as
// its own pass, in tonemap/tonemap.slang.
//
// This is a comprehensive port: skinning, instancing, IBL, tiled punctual
// lighting, cascaded shadows, normal mapping, KHR_texture_transform,
// alpha-mask, and unlit materials.

// Must match CASCADE_COUNT in forward.rs — there are exactly this many
// light_space* matrices below, so the cascade count is not variable.
static const int CASCADE_COUNT = 3;

// Must match render::tile_grid::TILE_SIZE in forward.rs — the CPU bins
// lights into a TILE_SIZE x TILE_SIZE screen-space grid, and punctual_lighting
// below must divide fragCoord by the same constant to land in the right tile.
static const uint TILE_SIZE = 16;

struct FrameUniforms
{
    float4x4 view_proj;
    float4x4 light_space;
    float4x4 light_space_1;
    float4x4 light_space_2;
    float4 light_dir_ambient;  // xyz: dir towards light, w: ambient
    float4 light_color_intensity; // rgb: color, w: intensity
    float4 camera_position;    // xyz: pos, w: punctual light count
    float4 cascade_splits;     // x,y: splits, z: tile_w, w: tile_h
};

struct PrimUniforms
{
    float4x4 model;
    float4x4 normal_matrix;
    float4 base_color;
    float4 material_factors;  // x: metallic, y: roughness, z: occlusion, w: normal scale
    float4 emissive_factor;   // rgb: emissive, w: MASK cutoff
    float4 base_uv_row0;
    float4 base_uv_row1;
    float4 material_flags;    // x: unlit, y: uv mask bits
};

struct IblParams
{
    float4 enabled_maxmip_intensity;
};

[vk::binding(0, 0)] ConstantBuffer<PrimUniforms> prim;
[vk::binding(1, 0)] Texture2DArray<float> shadowMap;
[vk::binding(2, 0)] SamplerComparisonState shadowSampler;
[vk::binding(3, 0)] Texture2D<float4> baseColorTex;
[vk::binding(4, 0)] SamplerState baseColorSampler;
[vk::binding(5, 0)] Texture2D<float4> metalRoughTex;
[vk::binding(6, 0)] SamplerState metalRoughSampler;
[vk::binding(7, 0)] Texture2D<float4> normalTex;
[vk::binding(8, 0)] SamplerState normalSampler;
[vk::binding(9, 0)] Texture2D<float4> emissiveTex;
[vk::binding(10, 0)] SamplerState emissiveSampler;
[vk::binding(11, 0)] Texture2D<float4> occlusionTex;
[vk::binding(12, 0)] SamplerState occlusionSampler;
[vk::binding(13, 0)] StructuredBuffer<float4x4> jointMatrices;

[vk::binding(0, 1)] ConstantBuffer<IblParams> iblParams;
[vk::binding(1, 1)] TextureCube<float4> irradianceMap;
[vk::binding(2, 1)] TextureCube<float4> prefilteredMap;
[vk::binding(3, 1)] Texture2D<float4> brdfLut;
[vk::binding(4, 1)] SamplerState iblSampler;

[vk::binding(0, 2)] ConstantBuffer<FrameUniforms> frame;

[vk::binding(0, 3)] StructuredBuffer<float4> punctualLights;
[vk::binding(0, 4)] StructuredBuffer<uint2> tileLightGrid;
[vk::binding(1, 4)] StructuredBuffer<uint> tileLightIndices;

[vk::binding(0, 1)] ConstantBuffer<uint4> shadowCascadeIndex;

struct VsIn
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
    float4 joints : JOINTS;
    float4 weights : WEIGHTS;
    float4 color : COLOR;
    float2 uv1 : TEXCOORD1;
    float4 instance0 : INSTANCE0;
    float4 instance1 : INSTANCE1;
    float4 instance2 : INSTANCE2;
    float4 instance3 : INSTANCE3;
};

float4x4 instance_matrix(VsIn In)
{
    return float4x4(In.instance0, In.instance1, In.instance2, In.instance3);
}

float3x3 instance_cofactor(VsIn In)
{
    float4x4 m = instance_matrix(In);
    float3 c0 = m[0].xyz;
    float3 c1 = m[1].xyz;
    float3 c2 = m[2].xyz;
    return float3x3(cross(c1, c2), cross(c2, c0), cross(c0, c1));
}

float4x4 skin_matrix(VsIn In)
{
    float4 w = In.weights;
    float total = w.x + w.y + w.z + w.w;
    if (total <= 0.0001) return prim.model;
    // Scale each joint matrix by its weight, then sum.
    float4x4 m = jointMatrices[uint(In.joints.x)] * w.x;
    m += jointMatrices[uint(In.joints.y)] * w.y;
    m += jointMatrices[uint(In.joints.z)] * w.z;
    m += jointMatrices[uint(In.joints.w)] * w.w;
    return m * (1.0 / total);
}

struct VsOut
{
    float4 svPosition : SV_Position;
    float3 worldNormal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 lightSpacePos : TEXCOORD2;
    float4 worldTangent : TEXCOORD3;
    float3 worldPosition : TEXCOORD4;
    float viewDepth : TEXCOORD5;
    float4 vertexColor : TEXCOORD6;
    float2 uv1 : TEXCOORD7;
};

[shader("vertex")]
VsOut vs_main(VsIn In)
{
    VsOut o;
    float4x4 model = mul(instance_matrix(In), skin_matrix(In));
    float4 worldPos = mul(model, float4(In.position, 1.0));
    o.svPosition = mul(frame.view_proj, worldPos);
    if (In.weights.x + In.weights.y + In.weights.z + In.weights.w > 0.0001)
    {
        o.worldNormal = normalize(mul(model, float4(In.normal, 0.0)).xyz);
    }
    else
    {
        float3 nModel = mul(prim.normal_matrix, float4(In.normal, 0.0)).xyz;
        o.worldNormal = normalize(mul(instance_cofactor(In), nModel));
    }
    o.worldTangent = float4(normalize(mul(model, float4(In.tangent.xyz, 0.0)).xyz), In.tangent.w);
    o.uv = In.uv;
    o.lightSpacePos = mul(frame.light_space, worldPos);
    o.worldPosition = worldPos.xyz;
    o.viewDepth = distance(worldPos.xyz, frame.camera_position.xyz);
    o.vertexColor = In.color;
    o.uv1 = In.uv1;
    return o;
}

[shader("vertex")]
float4 vs_shadow(VsIn In) : SV_Position
{
    float4 world = mul(mul(instance_matrix(In), skin_matrix(In)), float4(In.position, 1.0));
    uint cascade = shadowCascadeIndex.x;
    if (cascade == 1u) return mul(frame.light_space_1, world);
    if (cascade == 2u) return mul(frame.light_space_2, world);
    return mul(frame.light_space, world);
}

struct VsShadowMaskedOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VsShadowMaskedOut vs_shadow_masked(VsIn In)
{
    VsShadowMaskedOut o;
    float4 world = mul(mul(instance_matrix(In), skin_matrix(In)), float4(In.position, 1.0));
    uint cascade = shadowCascadeIndex.x;
    if (cascade == 1u) o.pos = mul(frame.light_space_1, world);
    else if (cascade == 2u) o.pos = mul(frame.light_space_2, world);
    else o.pos = mul(frame.light_space, world);
    o.uv = In.uv;
    return o;
}

[shader("fragment")]
void fs_shadow_masked(VsShadowMaskedOut In)
{
    float2 uv = float2(
        prim.base_uv_row0.x * In.uv.x + prim.base_uv_row0.y * In.uv.y + prim.base_uv_row0.z,
        prim.base_uv_row1.x * In.uv.x + prim.base_uv_row1.y * In.uv.y + prim.base_uv_row1.z);
    float alpha = prim.base_color.a * baseColorTex.Sample(baseColorSampler, uv, 0).a;
    if (alpha < prim.emissive_factor.w) discard;
}

float4x4 light_space_for(int cascade)
{
    if (cascade == 1) return frame.light_space_1;
    if (cascade == 2) return frame.light_space_2;
    return frame.light_space;
}

// Returns visibility: 1 = fully lit, 0 = fully occluded — the inverse of
// common/cascaded_shadow.slang's calc_cascaded_shadow.
float shadow_visibility(float viewDepth, float3 worldPos, float nDotL)
{
    int selected = 0;
    if (viewDepth > frame.cascade_splits.x) selected = 1;
    if (viewDepth > frame.cascade_splits.y) selected = 2;
    if (selected > CASCADE_COUNT - 1) selected = CASCADE_COUNT - 1;

    // A fragment closer than roughly half the near split can fall outside
    // the selected cascade's box (cascade 0 hugs the camera focus point,
    // not the camera itself - see render/cascades.rs's module doc). Retry
    // progressively coarser cascades before giving up, so the shader only
    // reports "fully lit" once every remaining cascade has also missed.
    float3 proj = float3(0.0, 0.0, 0.0);
    float2 uv = float2(0.0, 0.0);
    int cascade = selected;
    bool inBounds = false;
    for (; cascade < CASCADE_COUNT; cascade++)
    {
        float4 lightSpacePos = mul(light_space_for(cascade), float4(worldPos, 1.0));
        proj = lightSpacePos.xyz / lightSpacePos.w;
        uv = float2(proj.x * 0.5 + 0.5, 0.5 - proj.y * 0.5);
        if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0 && proj.z <= 1.0)
        {
            inBounds = true;
            break;
        }
    }
    if (!inBounds) return 1.0;

    float bias = clamp(0.002 * (1.0 - nDotL) + 0.0005, 0.0005, 0.004);

    uint shadowWidth, shadowHeight, shadowLayers;
    shadowMap.GetDimensions(shadowWidth, shadowHeight, shadowLayers);
    float texel = 1.0 / float(shadowWidth);
    float sum = 0.0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float2 offset = float2(float(x), float(y)) * texel;
            sum += shadowMap.SampleCmpLevelZero(shadowSampler, float3(uv + offset, float(cascade)), proj.z - bias);
        }
    }
    return sum / 9.0;
}

static const float3 SKY_ZENITH = float3(0.09, 0.16, 0.35);
static const float3 SKY_HORIZON = float3(0.55, 0.62, 0.72);
static const float3 SKY_GROUND = float3(0.18, 0.16, 0.15);

float3 sky_radiance(float3 dir, bool withSun)
{
    float3 color;
    if (dir.y >= 0.0)
        color = lerp(SKY_HORIZON, SKY_ZENITH, pow(clamp(dir.y, 0.0, 1.0), 0.7));
    else
        color = lerp(SKY_HORIZON, SKY_GROUND, clamp(-dir.y * 3.0, 0.0, 1.0));
    if (withSun)
    {
        float3 l = normalize(frame.light_dir_ambient.xyz);
        float cosSun = max(dot(dir, l), 0.0);
        float sun = pow(cosSun, 1200.0) * 24.0 + pow(cosSun, 48.0) * 0.5;
        color += float3(1.0, 0.95, 0.85) * sun * (frame.light_color_intensity.w * 0.4);
    }
    return color;
}

float3 hemisphere_irradiance(float3 n)
{
    float3 sky = lerp(SKY_HORIZON, SKY_ZENITH, pow(clamp(n.y, 0.0, 1.0), 0.7));
    return lerp(SKY_GROUND * 0.7, sky, clamp(n.y * 0.5 + 0.5, 0.0, 1.0));
}

float3 env_brdf_approx(float3 f0, float roughness, float nDotV)
{
    float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = float4(roughness) * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + float3(ab.y);
}

float3 punctual_lighting(float3 worldPos, float3 n, float3 v, float3 albedo,
    float metallic, float roughness, float3 f0, float4 fragCoord)
{
    float3 total = float3(0.0);
    uint tileX = uint(fragCoord.x) / TILE_SIZE;
    uint tileY = uint(fragCoord.y) / TILE_SIZE;
    uint tileW = uint(frame.cascade_splits.z);
    uint tileH = uint(frame.cascade_splits.w);
    int totalLights = int(frame.camera_position.w);
    int tileCount = totalLights;
    uint tileOffset = 0u;
    int count = 0;
    if (tileW != 0u)
    {
        uint tx = min(tileX, max(tileW, 1u) - 1u);
        uint ty = min(tileY, max(tileH, 1u) - 1u);
        uint tileIndex = ty * tileW + tx;
        uint2 gridEntry = tileLightGrid[tileIndex];
        count = int(gridEntry.x);
        tileOffset = gridEntry.y;
        // `count <= 0` is the CPU binner finding no light for this tile.
        // `tile_grid.rs`'s `tile_rect_for_light` is not yet a fully
        // conservative bound in every case (see its doc comment), so this
        // stays the safety net rather than "iterate nothing".
        if (count > 0) tileCount = count;
    }

    for (uint j = 0u; j < uint(tileCount); j++)
    {
        uint lightIdx = (count > 0) ? tileLightIndices[tileOffset + j] : j;
        float4 a = punctualLights[lightIdx * 4u];
        float4 b = punctualLights[lightIdx * 4u + 1u];
        float4 cvec = punctualLights[lightIdx * 4u + 2u];
        float4 dvec = punctualLights[lightIdx * 4u + 3u];
        float kind = a.w;
        float3 l;
        float attenuation = 1.0;
        if (kind > 2.5)
        {
            l = normalize(-cvec.xyz);
        }
        else
        {
            float3 toLight = a.xyz - worldPos;
            float dist = max(length(toLight), 1e-4);
            l = toLight / dist;
            attenuation = 1.0 / (dist * dist);
            float range = b.w;
            if (range > 0.0)
            {
                float k = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
                attenuation *= k * k;
            }
            if (kind > 1.5)
            {
                float cosAngle = dot(normalize(cvec.xyz), -l);
                attenuation *= smoothstep(dvec.x, cvec.w, cosAngle);
            }
        }
        if (attenuation <= 0.0) continue;
        total += brdf_direct(n, v, l, albedo, metallic, roughness, f0, b.rgb * attenuation);
    }
    return total;
}

[shader("fragment")]
float4 fs_main(VsOut In) : SV_Target
{
    uint uvMask = uint(prim.material_flags.y);
    float2 baseIn = (uvMask & 1u) != 0u ? In.uv1 : In.uv;
    float2 mrIn = (uvMask & 2u) != 0u ? In.uv1 : In.uv;
    float2 normalIn = (uvMask & 4u) != 0u ? In.uv1 : In.uv;
    float2 emissiveIn = (uvMask & 8u) != 0u ? In.uv1 : In.uv;
    float2 occlusionIn = (uvMask & 16u) != 0u ? In.uv1 : In.uv;

    float2 baseUv = float2(
        prim.base_uv_row0.x * baseIn.x + prim.base_uv_row0.y * baseIn.y + prim.base_uv_row0.z,
        prim.base_uv_row1.x * baseIn.x + prim.base_uv_row1.y * baseIn.y + prim.base_uv_row1.z);

    float4 baseSample = baseColorTex.Sample(baseColorSampler, baseUv);
    float4 mrSample = metalRoughTex.Sample(metalRoughSampler, mrIn);
    float4 normalSample = normalTex.Sample(normalSampler, normalIn);
    float4 emissiveSample = emissiveTex.Sample(emissiveSampler, emissiveIn);
    float4 occlusionSample = occlusionTex.Sample(occlusionSampler, occlusionIn);

    float4 albedo = prim.base_color * baseSample * In.vertexColor;
    if (albedo.a < prim.emissive_factor.w) discard;
    if (prim.material_flags.x > 0.5) return albedo;

    float metallic = clamp(prim.material_factors.x * mrSample.b, 0.0, 1.0);
    float roughness = clamp(prim.material_factors.y * mrSample.g, 0.045, 1.0);
    float occlusion = lerp(1.0, occlusionSample.r, prim.material_factors.z);
    float3 emissive = prim.emissive_factor.rgb * emissiveSample.rgb;

    float3 nGeom = normalize(In.worldNormal);
    float3 t = normalize(In.worldTangent.xyz - nGeom * dot(nGeom, In.worldTangent.xyz));
    float3 b = cross(nGeom, t) * In.worldTangent.w;
    float3 nTs = normalSample.xyz * 2.0 - 1.0;
    nTs = float3(nTs.xy * prim.material_factors.w, nTs.z);
    float3 n = normalize(mul(float3x3(t, b, nGeom), nTs));

    float3 l = normalize(frame.light_dir_ambient.xyz);
    float3 v = normalize(frame.camera_position.xyz - In.worldPosition);

    float3 f0 = lerp(float3(0.04), albedo.rgb, metallic);
    float3 radiance = frame.light_color_intensity.rgb * frame.light_color_intensity.w;
    float nDotL = max(dot(n, l), 0.0);

    float3 directLight = brdf_direct(n, v, l, albedo.rgb, metallic, roughness, f0, radiance);
    float visibility = shadow_visibility(In.viewDepth, In.worldPosition, nDotL);
    directLight *= visibility;

    float3 punctual = punctual_lighting(In.worldPosition, n, v, albedo.rgb, metallic, roughness, f0, In.svPosition);

    // IBL ambient. Both the prefiltered-environment path and the analytic
    // sky/ground fallback are evaluated unconditionally and picked between
    // with a ternary (not a branch) - the cube samples use explicit LODs, so
    // this is legal, and it keeps the no-environment path bit-identical to
    // what the golden images were taken against.
    float3 reflected = reflect(-v, n);
    float nDotV = max(dot(n, v), 1e-4);
    float3 kSIbl = fresnel_schlick(nDotV, f0);
    float envIntensity = iblParams.enabled_maxmip_intensity.z;
    float iblStrength = frame.light_dir_ambient.w;
    bool envEnabled = iblParams.enabled_maxmip_intensity.x > 0.5;

    float3 irradianceAnalytic = hemisphere_irradiance(n);
    float3 irradianceEnv = irradianceMap.SampleLevel(iblSampler, n, 0.0).xyz * envIntensity;
    float3 irradiance = envEnabled ? irradianceEnv : irradianceAnalytic;
    float3 diffuseIbl = (float3(1.0) - kSIbl) * (1.0 - metallic) * albedo.rgb * irradiance;

    float3 prefiltered = prefilteredMap.SampleLevel(iblSampler, reflected, roughness * iblParams.enabled_maxmip_intensity.y).xyz * envIntensity;
    float2 brdfLutSample = brdfLut.Sample(iblSampler, float2(max(dot(n, v), 0.0), roughness)).rg;
    float3 specularSplitSum = prefiltered * (f0 * brdfLutSample.x + brdfLutSample.y);
    float3 envAnalytic = lerp(sky_radiance(reflected, true), irradianceAnalytic, roughness * roughness);
    float3 specularAnalytic = envAnalytic * env_brdf_approx(f0, roughness, nDotV);
    float3 specularIbl = envEnabled ? specularSplitSum : specularAnalytic;

    float3 ambient = iblStrength * occlusion * (diffuseIbl + specularIbl);

    float3 color = directLight + punctual + ambient + emissive;
    return float4(color, albedo.a);
}