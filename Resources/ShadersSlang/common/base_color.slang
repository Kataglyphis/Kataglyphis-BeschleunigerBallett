import scene_types;

// glTF base colour rule: base colour = baseColorFactor * sampled texture.
// `material.diffuse` holds the factor (see GltfLoader.cpp's fromGltfMaterial);
// mirrors forward.slang's `prim.base_color * baseSample`.
//
// Deliberately its own module (not folded into material_fetch.slang): the
// ray tracing / path tracing entry points already declare their own
// `objectDescription` binding and cannot also `import material_fetch`
// (which declares the same binding again) without an ambiguous-reference
// compile error. This module has no bindings, so every shading path can
// import it.
float3 base_color(ObjMaterial material, float3 sampled)
{
    return sampled * material.diffuse;
}
