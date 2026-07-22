#version 460 core

#extension GL_EXT_multiview : require
#extension GL_ARB_shading_language_include : require

#include "/host_device_shared.hpp"
#include "/bindings.hpp"

// Single-pass layered cascade rendering: gl_ViewIndex IS the cascade, so the
// whole transform happens here and the old pass-through geometry stage is
// gone (it only re-multiplied by the cascade matrix).

layout (location = 0) in vec3 pos;

layout (std140, binding = UNIFORM_LIGHT_MATRICES_BINDING) uniform LightSpaceMatrices
{
    mat4 lightSpaceMatrices[NUM_CASCADES];
};

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint cascadeIndex;// retained for push-constant layout stability; unused
};

void main() {
    uint cascade = min(uint(gl_ViewIndex), uint(NUM_CASCADES - 1));
    gl_Position = lightSpaceMatrices[cascade] * model * vec4(pos, 1.0f);
}
