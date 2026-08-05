import scene_types;

// Shared glTF material fetch for the forward (rasterizer), deferred (geometry),
// and shadow fragment shaders. Sole source for this shader. Uses scene_types
// (NOT rt_types) to avoid pulling ray tracing capabilities into
// rasterization shaders (which conflict with `discard`). Owns the
// objectDescription binding and the two lookups built on it
// (fetch_object_description, fetch_material); the pure, binding-free
// material rules built on the fetched ObjMaterial live in
// common/material_rules.slang instead, so the ray tracing / path tracing
// entry points - which declare their own objectDescription binding and
// cannot also import this module - can still use them.

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