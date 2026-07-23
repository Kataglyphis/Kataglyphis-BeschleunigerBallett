#version 460 core

#extension GL_EXT_multiview : require
#extension GL_ARB_shading_language_include : require

#include "/host_device_shared.hpp"
#include "/bindings.hpp"

// Single-pass layered cascade rendering: gl_ViewIndex IS the cascade, so the
// whole transform happens here and the old pass-through geometry stage is
// gone (it only re-multiplied by the cascade matrix).

layout (location = 0) in vec3 pos;
// UV forwarded ONLY so the fragment stage can alpha-test MASK casters (glTF
// alphaMode MASK / cut-out foliage). Opaque casters ignore it. Location 3 is
// the engine Vertex's texture_coords slot, matching the forward vertex shader.
layout (location = 3) in vec2 tex_coords;

// Light matrices move to set 1 so set 0 can bind the SHARED render descriptor
// set (materials + textures + object descriptions) the forward pass uses, which
// the alpha test in the fragment stage needs.
layout (std140, set = 1, binding = UNIFORM_LIGHT_MATRICES_BINDING) uniform LightSpaceMatrices
{
    mat4 lightSpaceMatrices[NUM_CASCADES];
};

layout(push_constant) uniform PushConstants {
    mat4 model;
    // Was cascadeIndex (unused - the VS derives the cascade from gl_ViewIndex).
    // Repurposed as the per-draw object index so the fragment stage can look up
    // the material, exactly as the forward pass does. Same 4-byte slot.
    uint objectIndex;
};

layout (location = 0) out vec2 fragUV;

void main() {
    uint cascade = min(uint(gl_ViewIndex), uint(NUM_CASCADES - 1));
    fragUV = tex_coords;
    gl_Position = lightSpaceMatrices[cascade] * model * vec4(pos, 1.0f);
}
