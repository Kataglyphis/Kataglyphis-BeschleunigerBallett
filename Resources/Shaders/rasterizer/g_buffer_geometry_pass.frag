#version 460
#extension GL_ARB_shading_language_include : require

#include "/material.glsl"

#include "/host_device_shared.hpp"
#include "/bindings.hpp"

layout(location = 0) out vec3   g_position;
layout(location = 1) out vec3   g_normal;
layout(location = 2) out vec3   g_albedo;
layout(location = 3) out vec3   g_material_id;

#ifdef KAT_VULKAN
layout(location = 0) in vec2     tex_coords;
layout(location = 1) in vec3     frag_pos;
layout(location = 2) in vec3     normal;

layout(binding = 1) uniform MaterialBlock {
    Material materials[MAX_MATERIALS];
};

layout(binding = 2) uniform sampler2D model_textures[MAX_TEXTURE_COUNT];
#else
in vec2     tex_coords;
in vec3     frag_pos;
in vec3     normal;

uniform Material materials[MAX_MATERIALS];

//all textures from the current model
uniform sampler2D model_textures[MAX_TEXTURE_COUNT];
#endif

layout(std430, binding = STORAGE_BUFFER_MATERIAL_ID_BINDING) buffer materialIndexPerPrimitive
{
    vec4 materialIndex_SSBO[];
};

void main() {
    
    int mat_ID      = int(materialIndex_SSBO[gl_PrimitiveID].r);
    uint tex_ID     = materials[mat_ID].textureID;

	g_position      = frag_pos;
	g_normal        = normalize(normal);
    g_material_id   = vec3(mat_ID);
    // keep in mind whether to use textures here or not ....
    g_albedo        = materials[mat_ID].diffuse;//texture(model_textures[tex_ID], tex_coords).rgb;//

}