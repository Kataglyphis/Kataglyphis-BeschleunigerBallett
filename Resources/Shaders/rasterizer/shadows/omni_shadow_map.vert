#version 460

layout (location = 0) in vec3 pos;

#ifdef KAT_VULKAN
layout(push_constant) uniform PushConstants {
	mat4 model;
};
#else
uniform mat4 model;
#endif

void main() {

	gl_Position = model * vec4(pos, 1.0f);

}