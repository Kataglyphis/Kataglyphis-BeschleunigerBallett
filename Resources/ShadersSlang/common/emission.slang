import scene_types;

// glTF emissive rule: emitted radiance = emissiveFactor (material.emission,
// KHR_materials_emissive_strength already folded in by GltfLoader.cpp's
// fromGltfMaterial) * sampled emissiveTexture, or just the factor when the
// material has no emissiveTexture (`sampled` is expected to be float3(1.0)
// in that case). Shared by all four shading paths so the multiply is written
// once - see base_color.slang for why this lives in its own no-binding
// module rather than material_fetch.slang (the RT/PT entry points cannot
// `import material_fetch` without an ambiguous-reference compile error);
// common/material_rules.slang follows the same pattern for its own rules.
float3 material_emission(ObjMaterial material, float3 sampled)
{
    return material.emission * sampled;
}
