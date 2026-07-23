#version 460

// Depth-only pass, but MASK casters (glTF alphaMode MASK / cut-out foliage) must
// drop their transparent texels so the card casts its silhouette, not the solid
// quad it is modelled as. Opaque casters (alphaCutoff < 0, the default) fall
// straight through with no work. Uses the SAME shared descriptor set (set 0) and
// material fetch as the forward pass (shader.frag); the extensions/includes
// mirror it so the buffer-reference material walk resolves identically.
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "hostDevice/host_device_shared_vars.hpp"
#include "scene/ObjMaterial.hpp"
#include "scene/Vertex.hpp"
#include "ObjectDescription.hpp"

layout (location = 0) in vec2 fragUV;

layout(set = 0, binding = OBJECT_DESCRIPTION_BINDING, scalar) buffer ObjectDescription_ {
    ObjectDescription i[];
} object_description;

layout(buffer_reference, scalar) buffer MaterialIDs {
    int i[];
}; // per triangle material id
layout(buffer_reference, scalar) buffer Materials {
    ObjMaterial m[];
}; // all materials

layout(set = 0, binding = SAMPLER_BINDING) uniform sampler texture_sampler[MAX_TEXTURE_COUNT];
layout(set = 0, binding = TEXTURES_BINDING) uniform texture2D tex[MAX_TEXTURE_COUNT];

// Must match directional_shadow_map.vert's push-constant block exactly - the
// block is shared across stages and GLSL requires identical layout in each.
layout(push_constant) uniform PushConstants {
    mat4 model;
    uint objectIndex;
};

void main() {
    ObjectDescription obj_res = object_description.i[objectIndex];
    MaterialIDs materialIDs   = MaterialIDs(obj_res.material_index_address);
    Materials materials       = Materials(obj_res.material_address);
    ObjMaterial material      = materials.m[materialIDs.i[gl_PrimitiveID]];

    // Only textured MASK materials alpha-test; everything else casts unchanged.
    if (material.alphaCutoff >= 0.0 && material.textureID >= 0) {
        int texture_id = clamp(int(obj_res.texture_offset) + material.textureID, 0, MAX_TEXTURE_COUNT - 1);
        float alpha = texture(sampler2D(tex[texture_id], texture_sampler[texture_id]), fragUV).a;
        if (alpha < material.alphaCutoff) { discard; }
    }
}
