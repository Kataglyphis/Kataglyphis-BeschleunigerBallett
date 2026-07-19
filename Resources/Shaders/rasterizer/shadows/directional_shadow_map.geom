#version 460 core

#extension GL_ARB_shading_language_include : require

#include "/host_device_shared.hpp"
#include "/bindings.hpp"

// The renderer records ONE render pass per cascade into a single-layer
// framebuffer, so this shader transforms by exactly the cascade being
// rendered. (It previously declared invocations = NUM_CASCADES and indexed
// by gl_InvocationID - correct only for single-pass layered rendering, which
// this engine does not do: every cascade's map received all cascades'
// geometry, so the sampled shadow term was meaningless.)
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout (std140, binding = UNIFORM_LIGHT_MATRICES_BINDING) uniform LightSpaceMatrices
{
    mat4 lightSpaceMatrices[NUM_CASCADES];
};

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint cascadeIndex;
};

void main()
{
    uint cascade = min(cascadeIndex, uint(NUM_CASCADES - 1));
    for (int i = 0; i < 3; ++i)
    {
        gl_Position = lightSpaceMatrices[cascade] * gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}