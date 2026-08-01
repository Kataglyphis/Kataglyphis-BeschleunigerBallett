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

    // Cascaded shadow maps
    vec4 cascadeSplits; // up to 4 cascades
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
