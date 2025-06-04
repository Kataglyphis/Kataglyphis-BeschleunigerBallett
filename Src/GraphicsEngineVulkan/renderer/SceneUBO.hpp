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
namespace KataglyphisRenderer::VulkanRendererInternals {
#endif

struct SceneUBO
{
    vec4 light_dir;
    vec4 view_dir;
    // xyz is position; w = fov
    vec4 cam_pos;
};
#ifdef __cplusplus
}// namespace KataglyphisRenderer::VulkanRendererInternals
#endif