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

// Push constant structure for the raster
struct PushConstantRasterizer
{
    mat4 model;// matrix of the instance
    // Inverse-transpose of model, precomputed on the CPU, ROWS only. The
    // Slang shaders need the normal matrix but Slang has no inverse() for the
    // SPIR-V target, so it is computed here and passed alongside the model
    // matrix. Only the upper 3x3 is ever read (it transforms a direction, not
    // a point), so a full mat4 would waste 16 bytes we do not have: Vulkan
    // guarantees only 128 bytes of push-constant space, and a full mat4 here
    // pushed this struct to 132. Row i is (invModel[0][i], invModel[1][i],
    // invModel[2][i], 0) - GLM is column-major, so this reads ACROSS columns.
    vec4 invModelRows[3];
    // Which entry of the object_description array this draw belongs to.
    //
    // The fragment shaders used to hard-code index 0 ("for now only one
    // object allowed"), so every model was shaded with the FIRST model's
    // material and geometry buffer addresses. With one model in the scene
    // that is invisible; with two it silently textures the second using the
    // first's materials.
    uint objectIndex;
};

#ifdef __cplusplus
}// namespace Kataglyphis::VulkanRendererInternals
#endif
