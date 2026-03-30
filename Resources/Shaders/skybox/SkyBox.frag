#version 460

#extension GL_ARB_shading_language_include : require

#include "/host_device_shared.hpp"

layout(location = 2) out vec3 g_albedo;
layout(location = 3) out vec3  g_material_id;

layout(location = 0) in vec3 tex_coords;
layout(location = 1) in vec4 world_pos;

// For Vulkan we need an explicit binding on samplers. Use the project prefix
// define KAT_VULKAN (passed as -DKAT_VULKAN by the compile script).
#ifdef KAT_VULKAN
layout(set = 0, binding = 1) uniform samplerCube skybox;
#else
layout(binding = 1) uniform samplerCube skybox;
#endif

void main()
  {

    g_albedo = texture(skybox, tex_coords).xyz;
    g_material_id = vec3(SKYBOX_MATERIAL_ID);

  }
