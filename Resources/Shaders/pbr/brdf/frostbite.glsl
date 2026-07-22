#include "ShadingLibrary.glsl"
#include "microfacet.glsl"

// source: https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf

vec3 F_Schlick(in vec3 f0, in float f90, in float u) { return f0 + (f90 - f0) * pow(1.f - u, 5.f); }

float FrostbiteDiffuse(float NdotV, float NdotL, float LdotH, float roughness)
{

    float energyBias = mix(0.0, 0.5, roughness);
    float energyFactor = mix(1.0, 1.0 / 1.51, roughness);
    float fd90 = energyBias + 2.0 * LdotH * LdotH * roughness;
    vec3 f0 = vec3(1.0f, 1.0f, 1.0f);
    float lightScatter = F_Schlick(f0, fd90, NdotL).r;
    float viewScatter = F_Schlick(f0, fd90, NdotL).r;

    return lightScatter * viewScatter * energyFactor;
}

float V_SmithGGXCorrelated(float NdotL, float NdotV, float alphaG)
{
    // Original formulation of G_SmithGGX Correlated
    // lambda_v = ( -1 + sqrt ( alphaG2 * (1 - NdotL2 ) / NdotL2 + 1)) * 0.5 f;
    // lambda_l = ( -1 + sqrt ( alphaG2 * (1 - NdotV2 ) / NdotV2 + 1)) * 0.5 f;
    // G_SmithGGXCorrelated = 1 / (1 + lambda_v + lambda_l );
    // V_SmithGGXCorrelated = G_SmithGGXCorrelated / (4.0 f * NdotL * NdotV );

    // This is the optimize version
    float alphaG2 = alphaG * alphaG;
    // Caution : the " NdotL *" and " NdotV *" are explicitely inversed , this is not a mistake .
    float Lambda_GGXV = NdotL * sqrt((-NdotV * alphaG2 + NdotV) * NdotV + alphaG2);
    float Lambda_GGXL = NdotV * sqrt((-NdotL * alphaG2 + NdotL) * NdotL + alphaG2);

    return 0.5f / (Lambda_GGXV + Lambda_GGXL);
}

float D_GGX(float NdotH, float m)
{
    // Divide by PI is apply later
    float m2 = m * m;
    float f = (NdotH * m2 - NdotH) * NdotH + 1;
    return m2 / (f * f);
}

vec3 evaluateFrostbitePBR(vec3 ambient,
  vec3 N,
  vec3 L,
  vec3 V,
  float roughness,
  vec3 light_color,
  float light_intensity)
{


    float NdotV = abs(dot(N, V)) + 1e-5f;// avoid artifact
    vec3 H = normalize(V + L);
    float LdotH = clamp(dot(L, H), 0.0f, 1.0f);
    float NdotH = clamp(dot(N, H), 0.0f, 1.0f);
    float NdotL = clamp(dot(N, L), 0.0f, 1.0f);

    // Diffuse term - the light factors were missing (same defect as every
    // sibling BRDF in this directory: only specular saw the light).
    vec3 color =
      light_color * light_intensity * FrostbiteDiffuse(NdotV, NdotL, LdotH, roughness) * (ambient / PI) * NdotL;
    //
    if (LdotH > 0 && NdotH > 0 && NdotL > 0) {

        // Specular BRDF
        // renge [0,1] from non-/to metallic
        float reflectence = 0.6;
        vec3 f0 = vec3(0.16f * reflectence * reflectence);
        // lets assume that at 90 degrees everything will be reflected
        float f90 = 1.f;
        vec3 F = F_Schlick(f0, f90, LdotH);
        float Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
        float D = D_GGX(NdotH, roughness);
        color += light_color * light_intensity * (1.f / PI) * evaluateCookTorrenceSpecularBRDF(D, Vis, F, NdotL, NdotV)
                 * NdotL;
    }

    return color;
}