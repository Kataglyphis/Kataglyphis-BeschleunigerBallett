// #extension GL_KHR_vulkan_glsl : enable

#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "../common/raycommon.glsl"

#include "../hostDevice/host_device_shared_vars.hpp"

#include "../../../Src/GraphicsEngineVulkan/renderer/GlobalUBO.hpp"
#include "../../../Src/GraphicsEngineVulkan/renderer/SceneUBO.hpp"

#include "../../../Src/GraphicsEngineVulkan/renderer/pushConstants/PushConstantRasterizer.hpp"

layout (location = 0) in vec3 positions; 
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 color;
layout (location = 3) in vec2 tex_coords;

layout (set = 0, binding = globalUBO_BINDING) uniform _GlobalUBO {
	GlobalUBO globalUBO;
};

layout (set = 0, binding = sceneUBO_BINDING) uniform _SceneUBO {
	SceneUBO sceneUBO;
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
	
	// -- WE ARE CALCULATION THE MVP WITH THE GLM LIBRARY WHO IS DESIGNED FOR OPENGL
	// -- THEREFORE TAKE THE DIFFERENT COORDINATE SYSTEMS INTO ACCOUNT
	gl_Position = globalUBO.projection * globalUBO.view * pc_raster.model * vec4(positions, 1.0f);
	
	worldPosition = vec3(pc_raster.model * vec4(positions, 1.0f));
	// worldPosition.y *= -1;
	shading_normal = vec3(transpose(inverse(pc_raster.model)) * vec4(normal, 0.0f));
	texture_coordinates = tex_coords;

	fragment_color = color;

}