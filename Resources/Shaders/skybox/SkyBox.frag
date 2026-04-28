#version 460

#extension GL_ARB_shading_language_include : require

#include "host_device_shared.hpp"

layout(location = 0) out vec4 g_albedo;

layout(location = 0) in vec3 tex_coords;

layout(set = 1, binding = 1) uniform samplerCube skybox;

void main()
  {

    g_albedo = vec4(texture(skybox, tex_coords).xyz, SKYBOX_MATERIAL_ID);

  }