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