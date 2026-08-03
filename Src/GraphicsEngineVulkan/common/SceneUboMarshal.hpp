#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <span>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "common/host_device_shared_vars.hpp"
#include "renderer/SceneUBO.hpp"

namespace Kataglyphis {

// Aspect ratio for the camera projection. Guards the zero-height swapchain
// extent VulkanRenderer::updateUniforms sees for a single frame while a
// window is being resized/minimized - dividing by it would otherwise poison
// the projection matrix with NaN/Inf.
constexpr auto aspectRatioOf(uint32_t width, uint32_t height) -> float
{
    return (height > 0) ? static_cast<float>(width) / static_cast<float>(height) : 1.0F;
}

// The camera projection matrix, in Vulkan's clip-space convention. glm's
// perspective() targets OpenGL's clip space, whose Y axis points the
// opposite way from Vulkan's, so [1][1] is flipped here.
//
// CascadedShadowMap.cpp:342-352 depends on the cascade light-space matrices
// NOT having this flip: they are built from glm::ortho with no flip applied,
// and the shadow pass disables culling specifically because flipping only
// the camera projection (and not the cascade matrices) reverses the two
// passes' triangle winding relative to each other. Do not add the flip to
// the cascade matrices to "match" this function.
inline auto makeVulkanProjection(float fovDegrees, float aspect, float nearPlane, float farPlane) -> glm::mat4
{
    glm::mat4 projection = glm::perspective(glm::radians(fovDegrees), aspect, nearPlane, farPlane);
    projection[1][1] *= -1;
    return projection;
}

// Clamps the GUI's PCF radius slider into [0, MAX_PCF_RADIUS] before it
// reaches SceneUBO. Without this, a negative guiValue cast straight to
// uint32_t (as VulkanRenderer::updateUniforms used to) wraps to a huge
// unsigned value, cascaded_shadow.slang's tap loop never executes, and the
// "no taps sampled" fallback (max(taps, 1.0) with visible == 0) reads as
// fully shadowed.
constexpr auto clampPcfRadius(int guiValue) -> uint32_t
{
    return static_cast<uint32_t>(std::clamp(guiValue, 0, MAX_PCF_RADIUS));
}

// Floors for the cloud volume's mesh scale and density multiplier. clouds.slang
// (:137) multiplies the mesh half-extents by the density multiplier to get
// cloud.radius, and the inverse model matrix (:150-155) divides by each
// component of cloud.radius - a zero in either the mesh scale or the density
// multiplier makes that division produce +-inf and NaN box intersections.
constexpr float kMinCloudMeshExtent = 1e-3F;
constexpr float kMinCloudDensityMultiplier = 1e-3F;

constexpr auto clampCloudMeshScale(glm::vec3 meshScale, float densityMultiplier) -> glm::vec4
{
    return { std::max(meshScale.x, kMinCloudMeshExtent),
        std::max(meshScale.y, kMinCloudMeshExtent),
        std::max(meshScale.z, kMinCloudMeshExtent),
        std::max(densityMultiplier, kMinCloudDensityMultiplier) };
}

// Writes up to MAX_CASCADES splits/matrices into the SceneUBO and returns the
// count actually written. shadowsEnabled false zeroes ubo.numCascades (the
// field the shaders gate on) but still writes the matrices/splits - keeping
// the last computed cascades in the UBO is harmless since nothing samples
// them while numCascades is 0, and avoids a second branch at every call site.
// activeCascades is truncated to the shorter of the two spans rather than
// trusting the assert below: NDEBUG builds compile it out, and a caller
// mismatch would otherwise read viewProjMatrices (or splitDepths) out of
// bounds instead of merely misbehaving.
inline auto fillSceneUboCascades(VulkanRendererInternals::SceneUBO &ubo,
  std::span<const float> splitDepths,
  std::span<const glm::mat4> viewProjMatrices,
  bool shadowsEnabled) -> uint32_t
{
    assert(splitDepths.size() == viewProjMatrices.size());

    const size_t activeCascades = std::min(
      { splitDepths.size(), viewProjMatrices.size(), static_cast<size_t>(MAX_CASCADES) });
    for (size_t i = 0; i < activeCascades; ++i) {
        ubo.cascadeSplits[static_cast<int>(i)] = splitDepths[i];
        ubo.cascadeLightSpaceMatrices[i] = viewProjMatrices[i];
    }

    const auto numCascades = shadowsEnabled ? static_cast<uint32_t>(activeCascades) : 0U;
    ubo.numCascades = numCascades;
    return numCascades;
}

}// namespace Kataglyphis
