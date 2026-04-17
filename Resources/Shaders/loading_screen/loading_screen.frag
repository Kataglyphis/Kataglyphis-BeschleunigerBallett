#version 460

layout(location = 0) in vec2 tex_coords;

layout(location = 0) out vec4 color;

layout(binding = 0) uniform sampler2D loading_screen;

void main() {

	color = vec4(texture(loading_screen, tex_coords).rgb, 1.0f);

}