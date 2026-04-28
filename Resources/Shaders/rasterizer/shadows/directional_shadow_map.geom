#version 460 core

#extension GL_ARB_shading_language_include : require

#include "/host_device_shared.hpp"
#include "/bindings.hpp"

layout(triangles, invocations = NUM_CASCADES) in;
layout(triangle_strip, max_vertices = 3) out;

layout (std140, binding = UNIFORM_LIGHT_MATRICES_BINDING) uniform LightSpaceMatrices
{
    mat4 lightSpaceMatrices[NUM_CASCADES];
};

void main()
{
    for (int i = 0; i < 3; ++i)
    {
        gl_Position = lightSpaceMatrices[gl_InvocationID] * gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}