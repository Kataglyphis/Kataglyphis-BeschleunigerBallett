import scene_types;

// Shared glTF material fetch for the forward (rasterizer), deferred (geometry),
// and shadow fragment shaders. Sole source for this shader. Uses scene_types
// (NOT rt_types) to avoid pulling ray tracing capabilities into
// rasterization shaders (which conflict with `discard`).

[vk::binding(2, 0)] StructuredBuffer<ObjectDescription> objectDescription;

// The object description for a per-draw object index.
ObjectDescription fetch_object_description(uint objectIndex)
{
    return objectDescription[objectIndex];
}

// The material of the current triangle for that object. The primitiveID
// is passed by the caller: PrimitiveIndex() in ray tracing shaders,
// SV_PrimitiveID in rasterization shaders (PrimitiveIndex() is RT-only in
// Slang, so it cannot be called here without pulling in RT capabilities).
ObjMaterial fetch_material(ObjectDescription obj, uint primitiveID)
{
    MaterialIDs* materialIDs = (MaterialIDs*)obj.material_index_address;
    Materials* materials = (Materials*)obj.material_address;
    return materials->m[materialIDs->i[primitiveID]];
}

// glTF KHR_texture_transform: scale then offset the UV before sampling.
float2 transform_uv(float2 uv, ObjMaterial material)
{
    return uv * material.uv_scale + material.uv_offset;
}

// glTF alphaMode MASK test. ObjMaterial.hpp defines a negative alphaCutoff as
// "not a MASK material" (OPAQUE/BLEND) - never discard. OBJ materials and
// every non-MASK glTF material default to -1, so they are unaffected. The
// tested alpha is the glTF product rule baseColorFactor.a * baseColorTexture.a
// (material.dissolve * sampledAlpha) - callers pass 1.0 for sampledAlpha when
// there is no base-colour texture, matching the spec's "texture term defaults
// to 1". OBJ materials always have dissolve == 1.0, so they are bit-unchanged.
bool alpha_masked_out(ObjMaterial material, float sampledAlpha)
{
    return material.alphaCutoff >= 0.0 && (sampledAlpha * material.dissolve) < material.alphaCutoff;
}