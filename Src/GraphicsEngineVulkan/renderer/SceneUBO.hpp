// this little "hack" is needed for using it on the
// CPU side as well for the GPU side :)
// inspired by the NVDIDIA tutorial:
// https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/

#ifdef __cplusplus
#pragma once
#include "common/host_device_shared_vars.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
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

struct DirectionalLightData {
    vec4 direction;
    vec4 color; // w = radiance
};

struct SceneUBO
{
    // Directional light
    DirectionalLightData dirLight;

    uint pcfRadius;
    float cascadedShadowIntensity;
    uint numCascades;

    // Slang compiles ConstantBuffer<SceneUBO> as std140 (emitted type
    // SceneUBO_std140), where a vec4 has base alignment 16, so cascadeSplits
    // starts at byte 48 on the GPU. glm's plain (non-aligned) vec4 gives the
    // host struct no such padding, so this member must be explicit here.
    // See SceneUboLayoutUnit and
    // `spirv-dis Resources/ShadersSlang/build/spirv/rasterizer/rasterizer.fs_main.spv`
    // to re-derive these offsets.
    uint _pad_std140_0;

    // Cascaded shadow maps
    vec4 cascadeSplits; // up to 4 cascades
#ifdef __cplusplus
    static_assert(MAX_CASCADES <= 4,
      "cascadeSplits is a single vec4 - a fourth-plus cascade would write past its end");
#endif
    mat4 cascadeLightSpaceMatrices[MAX_CASCADES];

    // Camera
    vec4 view_dir;
    // xyz is position; w = fov
    vec4 cam_pos;
    
    // Clouds
    vec4 cloudMovementDirection; // w = speed
    vec4 cloudMeshScale; // w = cloudScale
    vec4 cloudMeshOffset; // w = cloudDensity
    vec4 cloudParameters; // x = pillowness, y = cirrus_effect, z = powder_effect, w = numMarchSteps
};
#ifdef __cplusplus
}// namespace Kataglyphis::VulkanRendererInternals
#endif
