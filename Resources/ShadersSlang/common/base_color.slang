import scene_types;

// glTF base colour rule: base colour = baseColorFactor * sampled texture.
// `material.diffuse` holds the factor (see GltfLoader.cpp's fromGltfMaterial);
// mirrors forward.slang's `prim.base_color * baseSample`.
//
// Deliberately its own module (not folded into material_fetch.slang): this
// module has no bindings, so every shading path - rasterization, ray
// tracing and the ray-query compute kernel alike - can import it without
// pulling in a second copy of material_fetch.slang's `objectDescription`
// declaration. Same pattern common/material_rules.slang follows for its own
// no-binding material rules.
float3 base_color(ObjMaterial material, float3 sampled)
{
    return sampled * material.diffuse;
}

// glTF KHR_texture_transform: apply the T*R*S UV matrix (given as its two
// rows) before sampling. Lives here rather than material_fetch.slang for the
// same reason as base_color() above: this no-binding module can be imported
// by every shading path alongside material_fetch.slang.
float2 transform_uv(float2 uv, float3 row0, float3 row1)
{
    return float2(dot(float3(uv, 1.0), row0), dot(float3(uv, 1.0), row1));
}

// Base-colour slot's KHR_texture_transform, by definition: ObjMaterial's
// uv_transform_row0/_row1 are the base-colour pair (see ObjMaterial.hpp). The
// normal, metallic-roughness and emissive slots each have their own named
// accessor below, forwarding to the (uv, row0, row1) overload above with
// their own row pair — the raw overload is the shared implementation, never
// the call-site form for those three slots.
float2 transform_uv(float2 uv, ObjMaterial material)
{
    return transform_uv(uv, material.uv_transform_row0, material.uv_transform_row1);
}

// Normal slot's KHR_texture_transform.
float2 normal_uv(float2 uv, ObjMaterial material)
{
    return transform_uv(uv, material.normal_uv_transform_row0, material.normal_uv_transform_row1);
}

// Metallic-roughness slot's KHR_texture_transform.
float2 metallic_roughness_uv(float2 uv, ObjMaterial material)
{
    return transform_uv(uv, material.metallic_roughness_uv_transform_row0, material.metallic_roughness_uv_transform_row1);
}

// Emissive slot's KHR_texture_transform.
float2 emissive_uv(float2 uv, ObjMaterial material)
{
    return transform_uv(uv, material.emissive_uv_transform_row0, material.emissive_uv_transform_row1);
}
