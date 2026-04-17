#version 460

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_tex_coord;

#ifdef KAT_VULKAN
layout(push_constant) uniform PushConstants {
	mat4 model;
	mat4 view;
	mat4 projection;
};
layout(location = 0) out vec4 cloud_world_pos;
#else
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec4 cloud_world_pos;
#endif

void main() {
	
	cloud_world_pos = model * vec4(in_position, 1.0f);
	gl_Position = projection * view * model * vec4(in_position, 1.0f);

}