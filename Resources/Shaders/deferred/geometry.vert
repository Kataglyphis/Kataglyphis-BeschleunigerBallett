#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "raycommon.glsl"
#include "host_device_shared_vars.hpp"
#include "GlobalUBO.hpp"
#include "SceneUBO.hpp"
#include "pushConstants/PushConstantRasterizer.hpp"

layout (location = 0) in vec3 positions; 
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 color;
layout (location = 3) in vec2 tex_coords;

layout (set = 0, binding = globalUBO_BINDING) uniform _GlobalUBO {
	GlobalUBO globalUBO;
};

layout (push_constant) uniform _PushConstantRasterizer {
	PushConstantRasterizer pc_raster;
};

layout (location = 0) out vec2 texture_coordinates;
layout (location = 1) out vec3 shading_normal;
layout (location = 2) out vec3 fragment_color;
layout (location = 3) out vec3 worldPosition;

out gl_PerVertex
{
  vec4 gl_Position;
};

void main () {
	vec4 opengl_position = globalUBO.projection * globalUBO.view * pc_raster.model * vec4(positions, 1.0f);
	vec4 vulkan_position = vec4(opengl_position.x, -opengl_position.y, opengl_position.z, opengl_position.w);
	
	worldPosition = vec3(pc_raster.model * vec4(positions, 1.0f));
	worldPosition.y *= -1;
	shading_normal = vec3(transpose(inverse(pc_raster.model)) * vec4(normal, 0.0f));
	texture_coordinates = tex_coords;
	fragment_color = color;

	gl_Position = vulkan_position;
}
