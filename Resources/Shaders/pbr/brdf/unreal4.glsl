#include "ShadingLibrary.glsl"
#include "microfacet.glsl"

// (https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf)
// https://github.com/SaschaWillems/Vulkan/blob/master/data/shaders/glsl/pbrbasic/pbr.frag
float D_GGX_EPIC_GAMES(vec3 wh, vec3 N, float roughness)
{
    float dotNH = clamp(dot(normalize(wh), normalize(N)), 0.0f, 1.0);
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = dotNH * dotNH * (alpha2 - 1.0) + 1.0;
    return (alpha2) / (PI * denom * denom);
}

float G_GGX_EPIC_GAMES(vec3 wi, vec3 wo, vec3 N, float roughness)
{
    float dotNL = clamp(dot(normalize(wo), normalize(N)), 0.0, 1.0);
    float dotNV = clamp(dot(normalize(wi), normalize(N)), 0.0, 1.0);
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float GL = dotNL / (dotNL * (1.0 - k) + k);
    float GV = dotNV / (dotNV * (1.0 - k) + k);
    return GL * GV;
}

vec3 F_EPIC_GAMES(vec3 wi, vec3 wh, vec3 ambient_color, float metallic)
{

    float cosTheta = clamp(dot(wi, wh), 0.0f, 1.0f);
    vec3 F0 = mix(vec3(0.04), ambient_color, metallic);
    vec3 F = F0 + (1.0 - F0) * pow(2, (-5.55473 * cosTheta - 6.98316) * cosTheta);
    return F;
}

vec3 evaluteUnreal4PBR(vec3 ambient, vec3 N, vec3 L, vec3 V, float roughness, vec3 light_color, float light_intensity)
{

    float cosTheta_l = CosTheta(L, N);
    float cosTheta_v = CosTheta(V, N);

    // add lambertian diffuse term
    // Light factors were missing on the diffuse term (see pbrBook.glsl).
    vec3 color = light_color * light_intensity * LambertDiffuse(ambient) * CosTheta(L, N);

    vec3 wo = normalize(L);
    vec3 wi = normalize(V);

    float cosThetaO = AbsCosTheta(wo, N);
    float cosThetaI = AbsCosTheta(wi, N);
    vec3 wh = wi + wo;
    if (cosThetaI == 0 || cosThetaO == 0) return vec3(0);
    if (wh.x == 0 && wh.y == 0 && wh.z == 0) return vec3(0);
    wh = normalize(wh);

    float D = D_GGX_EPIC_GAMES(wh, N, roughness);
    float G = G_GGX_EPIC_GAMES(wi, wo, N, roughness);
    vec3 F = F_EPIC_GAMES(wi, wh, ambient, .3f);

    // add specular term
    if (cosTheta_l > 0 && cosTheta_v > 0) {
        color += light_color * light_intensity * evaluateCookTorrenceSpecularBRDF(D, G, F, cosTheta_l, cosTheta_v)
                 * cosTheta_l;
    }

    return color;
}