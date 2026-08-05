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

// glTF KHR_texture_transform: apply the T*R*S UV matrix (given as its two
// rows) before sampling. Lives here rather than material_fetch.slang for the
// same reason as base_color() above: the ray tracing / path tracing entry
// points cannot `import material_fetch` (duplicate objectDescription
// binding), but every shading path already imports this no-binding module.
float2 transform_uv(float2 uv, float3 row0, float3 row1)
{
    return float2(dot(float3(uv, 1.0), row0), dot(float3(uv, 1.0), row1));
}

// Base-colour slot's KHR_texture_transform, by definition: ObjMaterial's
// uv_transform_row0/_row1 are the base-colour pair (see ObjMaterial.hpp). The
// normal, metallic-roughness and emissive slots each have their own pair and
// call the (uv, row0, row1) overload above directly with those members.
float2 transform_uv(float2 uv, ObjMaterial material)
{
    return transform_uv(uv, material.uv_transform_row0, material.uv_transform_row1);
}
