// this little "hack" is needed for using it on the
// CPU side as well for the GPU side :)
// inspired by the NVDIDIA tutorial:
// https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/

#ifdef __cplusplus
#pragma once
#include <glm/glm.hpp>
// GLSL Type
using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat4 = glm::mat4;
using uint = unsigned int;
namespace Kataglyphis::VulkanRendererInternals {
#endif

struct PushConstantPathTracing
{
    vec4 clearColor;
    uint width;
    uint height;
};

#ifdef __cplusplus
}// namespace Kataglyphis::VulkanRendererInternals
#endif
