#version 460

#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_tex_coord;

layout(location = 0) out vec3 out_worldDir;

#ifdef KAT_VULKAN
layout(set = 0, binding = 0, std140) uniform Camera {
    mat4 projection;
    mat4 view;
} camera;
#else
layout(std140, binding = 0) uniform Camera {
    mat4 projection;
    mat4 view;
} camera;
#endif

void main() {
    vec4 clipPos = vec4(in_position.x, in_position.y, 1.0, 1.0);
    vec4 viewPos = inverse(camera.projection) * clipPos;
    vec3 viewDir = vec3(viewPos.xy, -1.0);
    out_worldDir = transpose(mat3(camera.view)) * viewDir;
    gl_Position = vec4(in_position.xy, 0.9999, 1.0);
}