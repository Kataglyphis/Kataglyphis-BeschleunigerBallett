#ifndef KATAGLYPHIS_MATERIAL_FETCH_GLSL
#define KATAGLYPHIS_MATERIAL_FETCH_GLSL

// Shared glTF material fetch for the forward (shader.frag), deferred
// (deferred/geometry.frag) and shadow (rasterizer/shadows/directional_shadow_map.frag)
// fragment shaders. Each used to declare the object-description buffer, the
// buffer-reference material blocks, and the same per-face walk inline; this
// removes three near-identical copies (the forward one even carried dead
// Vertices/Indices refs). FRAGMENT-only (fetch_material reads gl_PrimitiveID).
//
// The includer must already have in scope: the ObjMaterial and ObjectDescription
// structs, OBJECT_DESCRIPTION_BINDING, and the GL_EXT_scalar_block_layout +
// GL_EXT_buffer_reference2 extensions.

layout(set = 0, binding = OBJECT_DESCRIPTION_BINDING, scalar) buffer ObjectDescription_ {
    ObjectDescription i[];
} object_description;

layout(buffer_reference, scalar) buffer MaterialIDs {
    int i[];
};
layout(buffer_reference, scalar) buffer Materials {
    ObjMaterial m[];
};

// The object description for a per-draw object index (pushed as pc.objectIndex).
ObjectDescription fetch_object_description(uint objectIndex) {
    return object_description.i[objectIndex];
}

// The material of the current triangle (gl_PrimitiveID) for that object.
ObjMaterial fetch_material(ObjectDescription obj_res) {
    MaterialIDs materialIDs = MaterialIDs(obj_res.material_index_address);
    Materials materials     = Materials(obj_res.material_address);
    return materials.m[materialIDs.i[gl_PrimitiveID]];
}

// glTF KHR_texture_transform: scale then offset the UV before sampling the
// base-colour texture. Identity (uv_scale 1, uv_offset 0) returns uv unchanged,
// so materials without the extension sample exactly as before.
vec2 transform_uv(vec2 uv, ObjMaterial material) {
    return uv * material.uv_scale + material.uv_offset;
}

#endif
