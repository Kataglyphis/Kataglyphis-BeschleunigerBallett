#version 460 core

layout (location = 0) in vec3 pos;

layout(push_constant) uniform PushConstants {
	mat4 model;
};

void main() {
	gl_Position = model * vec4(pos, 1.0f);
}