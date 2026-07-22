#include "ShadingLibrary.glsl"
#include "microfacet.glsl"

//(https://pbr-book.org/3ed-2018/Reflection_Models/Microfacet_Models)

vec3 F_PBRT(vec3 wi, vec3 wh)
{

    // assuming dielectrics
    float cosThetaI = clamp(dot(normalize(wi), normalize(wh)), 0.0f, 1.0f);
    float etaI = 1.00029;// air at sea level
    float etaT = 1.5;// water 20 Degrees

    //<< Potentially swap indices of refraction >>
    /*bool entering = cosThetaI > 0.f;
    if (!entering) {
        float aux = etaI;
        etaI = etaT;
        etaT = aux;
        cosThetaI = abs(cosThetaI);
    }*/

    //<< Compute cosThetaT using Snell�s law >>
    float sinThetaI = sqrt(max(0.f, 1.f - cosThetaI * cosThetaI));
    float sinThetaT = etaI / etaT * sinThetaI;
    //<< Handle total internal reflection >>
    if (sinThetaT >= 1) return vec3(1);

    float cosThetaT = sqrt(max(0.f, 1.f - sinThetaT * sinThetaT));

    float Rparl = ((etaT * cosThetaI) - (etaI * cosThetaT)) / ((etaT * cosThetaI) + (etaI * cosThetaT));
    float Rperp = ((etaI * cosThetaI) - (etaT * cosThetaT)) / ((etaI * cosThetaI) + (etaT * cosThetaT));

    return vec3((Rparl * Rparl + Rperp * Rperp) / 2.f);
}

float RoughnessToAlpha_PBRT(float roughness)
{

    roughness = max(roughness, 1e-3);
    float x = log(roughness);
    return 1.62142f + 0.819955f * x + 0.1734f * x * x + 0.0171201f * x * x * x + 0.000640711f * x * x * x * x;
}

// Normal Distribution function ---------------------
float D_GGX_PBRT(vec3 wh, vec3 N, float roughness)
{

    float tan2Theta = Tan2Theta(wh, N);
    // catch infinity
    float infinity = 1.0 / 0.0;
    if (tan2Theta == infinity) return 0.;
    float alphax = RoughnessToAlpha_PBRT(roughness);
    float alphay = RoughnessToAlpha_PBRT(roughness);
    const float cos4Theta = Cos2Theta(wh, N) * Cos2Theta(wh, N);
    float e = (Cos2Phi(wh, N) / (alphax * alphax) + Sin2Phi(wh, N) / (alphay * alphay)) * tan2Theta;
    return 1.f / (PI * alphax * alphay * cos4Theta * (1.f + e) * (1.f + e));
}

float Lambda_PBRT(vec3 w, vec3 N, float roughness)
{

    float absTanTheta = abs(TanTheta(w, N));
    // catch infinity
    float infinity = 1.0 / 0.0;
    if (absTanTheta == infinity) return 0.;
    float alphax = RoughnessToAlpha_PBRT(roughness);
    float alphay = RoughnessToAlpha_PBRT(roughness);
    float alpha = sqrt(Cos2Phi(w, N) * alphax * alphax + Sin2Phi(w, N) * alphay * alphay);
    float alpha2Tan2Theta = (alpha * absTanTheta) * (alpha * absTanTheta);
    return (-1.f + sqrt(1.f + alpha2Tan2Theta)) / 2.f;
}

// Geometric Shadowing function -----------------------
float G_GGX_PBRT(vec3 wi, vec3 wo, vec3 N, float roughness)
{

    return 1.f / (1.f + Lambda_PBRT(wo, N, roughness) + Lambda_PBRT(wi, N, roughness));
}

vec3 evaluatePBRBooksPBR(vec3 ambient, vec3 N, vec3 L, vec3 V, float roughness, vec3 light_color, float light_intensity)
{

    // Lambertian diffuse term. The light factors were MISSING here for the
    // whole file's life: only the specular term below multiplied them, so
    // diffuse surfaces were lit by an implicit radiance-1 white light and
    // the GUI light slider only moved the highlights.
    vec3 color = light_color * light_intensity * LambertDiffuse(ambient) * CosTheta(L, N);

    vec3 wo = normalize(L);
    vec3 wi = normalize(V);
    vec3 wh = normalize(wi + wo);

    float cosThetaO = CosTheta(wo, N);
    float cosThetaI = CosTheta(wi, N);
    if (cosThetaI <= 0 || cosThetaO <= 0) return color;
    if (wh.x == 0 && wh.y == 0 && wh.z == 0) return color;

    if (CosTheta(L, N) > 0 && CosTheta(V, N) > 0) {

        // add specular term
        float D = D_GGX_PBRT(wh, N, roughness);
        float G = G_GGX_PBRT(wi, wo, N, roughness);
        vec3 F = F_PBRT(wi, wh);

        color += light_color * light_intensity
                 * evaluateCookTorrenceSpecularBRDF(D, G, F, CosTheta(L, N), CosTheta(V, N)) * CosTheta(L, N);
    }

    return color;
}