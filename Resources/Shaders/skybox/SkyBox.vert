#version 460

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_tex_coord;

layout(location = 0) out vec3 tex_coords;

// Support both Vulkan and OpenGL builds. Define KAT_VULKAN when compiling for Vulkan
// (the build script passes -DKAT_VULKAN). Vulkan requires descriptor set + binding;
// OpenGL uses only binding. Both accept a uniform block (UBO) with std140 layout.
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

    tex_coords = in_position;
    vec4 pos = camera.projection * camera.view * vec4(in_position, 1.0);
    gl_Position = pos.xyww;

}
