#include "ShadingLibrary.glsl"
#include "microfacet.glsl"

// source: �Models of Light Reflection for Computer Synthesized Pictures
//  https://dl.acm.org/doi/pdf/10.1145/563858.563893
//  take normalization term from Akine M�ller RealTimeRenderingThirdEdition page 111

vec3 evaluatePhong(vec3 ambient, vec3 N, vec3 L, vec3 V, vec3 light_color, float light_intensity)
{

    // ignore ambient term for now :)
    float ka = 0.f;
    float ks = 0.3f;

    // exponent controls roughness; is in [0,1] because of remapping
    float n = 0.5;
    // use remapping for better user controllability
    // use the same as in Call of Duty: Black Ops (source RealTimeRenderingThirdEdition page 340)
    float maximumSpecular = 8192;
    float remappedN = pow(maximumSpecular, ks);

    // Lambertian diffuse term - had intensity but not the light COLOR
    // (same defect family as the sibling BRDFs).
    vec3 color = light_color * light_intensity * LambertDiffuse(ambient);
    vec3 wh = normalize(L + V);

    // add specular term
    if (CosTheta(L, N) > 0 && CosTheta(V, N) > 0) {
        color +=
          light_color * light_intensity * ks * ((remappedN + 8.f) / (8.f * PI) * pow(CosTheta(wh, N), remappedN));
    }

    return color;
}