#version 460

layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 texture_coords;

layout(location = 0) out vec2 tex_coords;

void main() {
	
	tex_coords = texture_coords;
	gl_Position = vec4(pos, 1.0);

}