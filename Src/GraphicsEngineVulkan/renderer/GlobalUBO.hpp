// this little "hack" is needed for using it on the
// CPU side as well for the GPU side :)
// inspired by the NVDIDIA tutorial:
// https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/
#ifdef __cplusplus
#pragma once
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
// GLSL Type
using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat4 = glm::mat4;
using uint = unsigned int;
namespace Kataglyphis::VulkanRendererInternals {
#endif

// this will also be an input to our shaders !!
// which render stage doesn't need view,projection ?
struct GlobalUBO
{
    mat4 projection;
    mat4 view;
    // Precomputed on the CPU once per frame: the clouds compute shader
    // needs these per pixel, where inverse() is ruinously expensive.
    mat4 inv_projection;
    mat4 inv_view;
};
#ifdef __cplusplus
}// namespace Kataglyphis::VulkanRendererInternals
#endif