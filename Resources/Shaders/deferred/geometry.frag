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

// object_description + the material walk now live in the shared include.
#include "material_fetch.glsl"

layout(set = 0, binding = SAMPLER_BINDING) uniform sampler texture_sampler[MAX_TEXTURE_COUNT];
layout(set = 0, binding = TEXTURES_BINDING) uniform texture2D tex[MAX_TEXTURE_COUNT];


layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outAlbedo;
layout(location = 2) out vec4 outMaterial;

void main() {
    // Indexed per draw; see shader.frag.
    ObjectDescription obj_res = fetch_object_description(pc_raster.objectIndex);
    ObjMaterial material = fetch_material(obj_res);

    vec4 texColor;
    if (material.textureID >= 0) {
        // Model-local id + the model's slot in the flattened global array.
        int texture_id = clamp(int(obj_res.texture_offset) + material.textureID, 0, MAX_TEXTURE_COUNT - 1);
        texColor = texture(sampler2D(tex[texture_id], texture_sampler[texture_id]), transform_uv(texture_coordinates, material));
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

    // glTF COLOR_0 multiplies the base colour, matching the forward path
    // (shader.frag). Loaders write (1,1,1) for uncoloured meshes, so this is an
    // identity multiply there; fragment_color was forwarded but previously unused.
    texColor.rgb *= fragment_color;

    outNormal = vec4(normalize(shading_normal), 1.0);
    outAlbedo = texColor;

    // Roughness from the material's Blinn-Phong exponent - the SAME mapping
    // as the forward path (shader.frag), which the parity golden guards. The
    // lighting pass read material.r all along, but this pass wrote a
    // hard-coded 0.9 into it, so "deferred reads the material" was an
    // illusion. (Metallic, AO stay defaults.)
    outMaterial = vec4(clamp(sqrt(2.0 / (material.shininess + 2.0)), 0.045, 1.0), 0.0, 1.0, 1.0);
}
