#version 460

#extension GL_ARB_shading_language_include : require

#include "/host_device_shared.hpp"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_tex_coord;

#ifdef KAT_VULKAN
layout(location = 0) out vec2	tex_coords;
layout(location = 1) out vec3	frag_pos;
layout(location = 2) out vec3	normal;

layout(push_constant) uniform PushConstants {
	mat4 projection;                
	mat4 view;                                                            
	mat4 model;
	mat4 normal_model;
};
#else
out vec2	tex_coords;
out vec3	frag_pos;
out vec3	normal;

//uniform variables
uniform mat4 projection;                
uniform mat4 view;                                                            
uniform mat4 model;
uniform mat4 normal_model;
#endif

void main() {
	
	vec4 world_pos = model * vec4(in_position, 1.0);
	frag_pos = world_pos.xyz;
	tex_coords = in_tex_coord;

	gl_Position = projection * view * model * vec4(in_position, 1.0);
	//move matrix calculations to application part 
	normal = mat3(normal_model) * in_normal;

}