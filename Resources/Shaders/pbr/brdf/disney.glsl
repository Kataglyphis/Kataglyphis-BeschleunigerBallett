#include "ShadingLibrary.glsl"
#include "microfacet.glsl"

// the famous disney principled brdf model
// based on their paper
// (https://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf) brent
// burley even released some helpful shader code: https://github.com/wdas/brdf/blob/main/src/brdfs/disney.brdf
// https://schuttejoe.github.io/post/disneybsdf/
// https://blog.selfshadow.com/publications/s2015-shading-course/burley/s2015_pbs_disney_bsdf_notes.pdf

vec3 mon2lin(vec3 x)
{

    // https://de.wikipedia.org/wiki/Weber-Fechner-Gesetz
    // bring it in linear space when you want to to linear calculations
    return vec3(pow(x[0], 2.2), pow(x[1], 2.2), pow(x[2], 2.2));
}

vec3 DisneyDiffuse(vec3 ambient, vec3 L, vec3 V, vec3 N, float roughness, float subsurface)
{

    vec3 wh = normalize(L) + normalize(V);
    wh = normalize(wh);

    float cosTheta_d = clamp(dot(normalize(V), wh), -1.0f, 1.0f);
    float F_D90 = 0.5f + 2.f * roughness * cosTheta_d * cosTheta_d;

    vec3 lambertDiffuse = LambertDiffuse(ambient);

    // even if lit by the backside or viewed from backside
    // we want some ambient light!
    // catch it here to avoid brutal brightness because of
    // following attentuation terms :))
    if (CosTheta(L, N) <= 0 || CosTheta(V, N) <= 0) { return 0.3f * lambertDiffuse; }

    float F_light = 1.f + (F_D90 - 1.f) * pow(1.f - CosTheta(L, N), 5);
    float F_view = 1.f + (F_D90 - 1.f) * pow(1.f - CosTheta(V, N), 5);

    // add some "fake subscattering" here
    // strictly copied from their shader ...
    // Based on Hanrahan-Krueger brdf approximation of isotropic bssrdf
    // 1.25 scale is used to (roughly) preserve albedo
    // Fss90 used to "flatten" retroreflection based on roughness
    float Fss90 = Cos2Theta(L, wh) * roughness;
    float Fss = mix(1.0, Fss90, F_light) * mix(1.0, Fss90, F_view);
    float ss = 1.25 * (Fss * (1.f / (CosTheta(L, wh) + CosTheta(V, N)) - .5) + .5);

    return lambertDiffuse * mix(F_light * F_view, ss, subsurface);
}

// secondary lobe represents a clearcoat layer over the base: GTR1
// material, and is thus always isotropic and non - metallic.
float D_Disney_Secondary(vec3 wh, vec3 N, float a)
{

    float alpha2 = a * a;
    float denom3 = (1.f + (alpha2 - 1.f) * Cos2Theta(wh, N));

    return (alpha2 - 1.f) / (PI * log(alpha2) * denom3);
}

// the D function for their primary layer: GTR2aniso
// represents the base material and may be anisotropic and / or metallic
float D_Disney_Primary(vec3 wh, vec3 N, float roughness, float alphax, float alphay)
{

    float alphax2 = alphax * alphax;
    float alphay2 = alphay * alphay;

    float cos2Theta_h = Cos2Theta(wh, N);
    float h_dot_y = SinTheta(wh, N) * SinPhi(wh, N);
    float h_dot_x = SinTheta(wh, N) * CosPhi(wh, N);
    float h_dot_y2 = h_dot_y * h_dot_y;
    float h_dot_x2 = h_dot_x * h_dot_x;

    float denom3 = (h_dot_x2 / alphax2) + (h_dot_y2 / alphay2) + cos2Theta_h;

    return 1.f / (PI * alphax * alphay * denom3 * denom3);
}

float G_Disney(vec3 wi, vec3 wo, vec3 N, float roughness, float alphax, float alphay)
{
    return G2_GGX_SMITH(wi, wo, N, roughness, alphax, alphay);
}

float G_Disney_SmithGGXIso(vec3 wi, vec3 wo, vec3 N, float roughness)
{

    return G2_GGX_SMITH_ISOTROPIC(wi, wo, N, roughness);
}

vec3 evaluateDisneysPBR(vec3 ambient, vec3 N, vec3 L, vec3 V, float roughness, vec3 light_color, float light_intensity)
{

    // all values in [0,1]

    float anisotropic = 0.9;
    float metallic = 0.1f;
    // subsurface parameter blends between the base diffuse shape and one inspired by the HanrahanKrueger subsurface BR
    // This is useful for giving a subsurface appearance on distant objects
    // and on objects where the average scattering path length is small
    float subsurface = 0.0f;
    float specular = .9f;
    // a concession for artistic control that tints incident specular towards the base color.
    // Grazing specular is still achromatic.
    float specularTint = 0.7;
    // an additional grazing(absch�rfen) component, primarily intended for cloth; Glanz
    float sheen = 0.0;
    // amount to tint sheen towards base color.
    float sheenTint = 0.4;
    // a second, special-purpose specular lobe
    float clearcoat = 0.1f;
    // clearcoatGloss: controls clearcoat glossiness (0 = a �satin� appearance, 1 = a �gloss� appearance)
    float clearcoatGloss = 0.5;

    // ambient term brought to linear space for calculations
    vec3 linAmbient = mon2lin(1.5f * ambient);
    // based on number of seecells in the eye; the number of blue cells are the highest amount :))
    float luminance = 0.3f * linAmbient[0] + 0.6f * linAmbient[1] + 0.1f * linAmbient[2];
    // normalize lum. to isolate hue+sat
    vec3 ambientTint = luminance > 0 ? linAmbient / luminance : vec3(1);
    // lineary map specular coefficient to range [0.0,0.08]
    // this corresponds to IOR values in [1.0, 1.8] -> most common materials
    // don't add specular for metals
    float remappedSpecular = specular * 0.08f;
    vec3 Cspec0 = mix(remappedSpecular * mix(vec3(1), ambientTint, specularTint), linAmbient, metallic);
    vec3 Csheen = mix(vec3(1), ambientTint, sheenTint);


    // add diffuse term
    // add diffuse term before checking negativ cosinus!
    // we want some diffuse light for the sun even if cosinus negative
    // Light factors were missing here (see pbrBook.glsl - only specular saw
    // the light in every BRDF of this directory).
    vec3 diffuse = light_color * light_intensity * DisneyDiffuse(linAmbient, L, V, N, roughness, subsurface)
                   * (1.f - metallic) * CosTheta(L, N);

    // 1.) case: get lit by light from the backside
    // 2.) case: view it from the back
    // you also need to take care of the equal zero case; otherwise divide by zero problems
    if (CosTheta(L, N) <= 0 || CosTheta(V, N) <= 0) {
        // return diffuse;
        return diffuse;
    }

    // sheen
    vec3 Fsheen = pow(1.f - abs(CosTheta(L, N)), 5) * sheen * Csheen;

    vec3 SheenColor = Fsheen * (1.f - metallic);

    vec3 wh = normalize(L + V);

    // clearcoat (ior = 1.5 -> F0 = 0.04)
    float Dr = D_Disney_Secondary(wh, N, mix(0.1, 0.001, clearcoatGloss));
    float Fr = mix(.04, 1.0, pow(1.f - abs(CosTheta(L, N)), 5));
    // 0.25: fixed IOR of 1.5; polyurethane
    //
    float Gr = G_Disney_SmithGGXIso(normalize(V), normalize(L), N, 0.25);

    vec3 clearCoatColor = vec3(0.25f * clearcoat * Gr * Fr * Dr);

    // mapping to anisotropic alpha's; see paper
    float aspect = sqrt(1.f - 0.9f * anisotropic);
    float alphax = (roughness * roughness) / aspect;
    float alphay = (roughness * roughness) * aspect;

    // GTR2aniso
    float D = D_Disney_Primary(wh, N, roughness, alphax, alphay);
    // smith ggx aniso
    float G = G_Disney(normalize(V), normalize(L), N, roughness, alphax, alphay);
    // simple fresnel schlick approx
    vec3 F = mix(Cspec0, vec3(1), fresnel_schlick(CosTheta(wh, V), ambient, metallic));

    // add specular term
    vec3 specular_color = light_color * light_intensity
                          * evaluateCookTorrenceSpecularBRDF(D, G, F, CosTheta(L, N), CosTheta(V, N)) * CosTheta(L, N);

    return diffuse + specular_color + clearCoatColor + SheenColor;
}
