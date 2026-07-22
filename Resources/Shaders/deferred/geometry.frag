#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "raycommon.glsl"
#include "host_device_shared_vars.hpp"
#include "pushConstants/PushConstantRasterizer.hpp"
#include "SceneUBO.hpp"
#include "scene/ObjMaterial.hpp"
#include "scene/Vertex.hpp"
#include "ObjectDescription.hpp"

// Must match geometry.vert's declaration exactly - the block is shared across
// stages and GLSL requires identical layout in each.
layout (push_constant) uniform _PushConstantRasterizer {
	PushConstantRasterizer pc_raster;
};

layout (location = 0) in vec2 texture_coordinates;
layout (location = 1) in vec3 shading_normal;
layout (location = 2) in vec3 fragment_color;
layout (location = 3) in vec3 worldPosition;

layout(set = 0, binding = OBJECT_DESCRIPTION_BINDING, scalar) buffer ObjectDescription_ {
    ObjectDescription i[];
} object_description;

layout(buffer_reference, scalar) buffer Vertices { Vertex v[]; };
layout(buffer_reference, scalar) buffer Indices { ivec3 i[]; };
layout(buffer_reference, scalar) buffer MaterialIDs { int i[]; };
layout(buffer_reference, scalar) buffer Materials { ObjMaterial m[]; };

layout(set = 0, binding = SAMPLER_BINDING) uniform sampler texture_sampler[MAX_TEXTURE_COUNT];
layout(set = 0, binding = TEXTURES_BINDING) uniform texture2D tex[MAX_TEXTURE_COUNT];


layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outAlbedo;
layout(location = 2) out vec4 outMaterial;

void main() {
    // Indexed per draw; see shader.frag.
    ObjectDescription obj_res = object_description.i[pc_raster.objectIndex];
    MaterialIDs materialIDs = MaterialIDs(obj_res.material_index_address);
    Materials materials = Materials(obj_res.material_address);

    ObjMaterial material = materials.m[materialIDs.i[gl_PrimitiveID]];

    vec4 texColor;
    if (material.textureID >= 0) {
        // Model-local id + the model's slot in the flattened global array.
        int texture_id = clamp(int(obj_res.texture_offset) + material.textureID, 0, MAX_TEXTURE_COUNT - 1);
        texColor = texture(sampler2D(tex[texture_id], texture_sampler[texture_id]), texture_coordinates);
        // glTF alphaMode MASK uses the material's cutoff; everything else keeps the
        // pre-existing crude 0.1 near-transparent cull, so opaque/OBJ materials are
        // unchanged and the forward and deferred paths agree on MASK geometry.
        float alpha_cull = (material.alphaCutoff >= 0.0) ? material.alphaCutoff : 0.1;
        if(texColor.a < alpha_cull) discard;
    } else {
        // Untextured material - same clamp-to-slot-0 defect and same fix as
        // the forward path (shader.frag).
        texColor = vec4(material.diffuse, 1.0);
    }

    outNormal = vec4(normalize(shading_normal), 1.0);
    outAlbedo = texColor;

    // Roughness from the material's Blinn-Phong exponent - the SAME mapping
    // as the forward path (shader.frag), which the parity golden guards. The
    // lighting pass read material.r all along, but this pass wrote a
    // hard-coded 0.9 into it, so "deferred reads the material" was an
    // illusion. (Metallic, AO stay defaults.)
    outMaterial = vec4(clamp(sqrt(2.0 / (material.shininess + 2.0)), 0.045, 1.0), 0.0, 1.0, 1.0);
}
