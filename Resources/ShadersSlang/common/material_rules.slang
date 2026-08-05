import scene_types;

// Pure glTF material rules over an ObjMaterial value, no bindings - so every
// shading path, including the ray-query kernels, can import it without
// pulling in a second copy of material_fetch.slang's `objectDescription`
// declaration. Same pattern as
// base_color.slang/emission.slang/normal_map.slang.

// Roughness shared by every shading path (forward, deferred, ray-traced).
// A non-negative material.roughness is an authored glTF roughnessFactor and
// is used directly. A negative material.roughness (every OBJ material, which
// has no roughness input, only Ns) falls back to the Beckmann-style
// shininess -> roughness mapping. The 0.045 floor keeps the BRDF's specular
// lobe from collapsing to a singularity at shininess/roughness extremes.
float material_roughness(ObjMaterial material)
{
    return material.roughness >= 0.0
      ? clamp(material.roughness, 0.045, 1.0)
      : clamp(sqrt(2.0 / (material.shininess + 2.0)), 0.045, 1.0);
}

// glTF 2.0 SS3.9.2 pbrMetallicRoughness texture channel assignment: G =
// roughness, B = metallic, both multiplying the material's existing factors.
// A material with no metallic-roughness texture passes mrSample = float4(1.0),
// so this is exactly today's factors (material.metallic,
// material_roughness(material)) unchanged.
float2 material_metallic_roughness(ObjMaterial material, float4 mrSample)
{
    return float2(material.metallic * mrSample.b, material_roughness(material) * mrSample.g);
}

// glTF alphaMode MASK test. ObjMaterial.hpp defines a negative alphaCutoff as
// "not a MASK material" (OPAQUE/BLEND) - never discard. OBJ materials and
// every non-MASK glTF material default to -1, so they are unaffected. The
// tested alpha is the glTF product rule baseColorFactor.a * baseColorTexture.a
// * COLOR_0.a (material.dissolve * sampledAlpha, where sampledAlpha is the
// caller-computed product of the base-colour texture's alpha and the vertex
// COLOR_0 alpha) - callers pass just the COLOR_0 alpha for sampledAlpha when
// there is no base-colour texture, matching the spec's "texture term defaults
// to 1". OBJ materials always have dissolve == 1.0 and vertex color == 1.0,
// so they are bit-unchanged.
bool alpha_masked_out(ObjMaterial material, float sampledAlpha)
{
    return material.alphaCutoff >= 0.0 && (sampledAlpha * material.dissolve) < material.alphaCutoff;
}
