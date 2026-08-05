#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <span>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "common/LightDirection.hpp"
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
// CascadedShadowMap::buildGraphicsPipeline's cull-mode comment depends on the
// cascade light-space matrices NOT having this flip: they are built from
// glm::ortho with no flip applied, and the shadow pass disables culling
// specifically because flipping only the camera projection (and not the
// cascade matrices) reverses the two passes' triangle winding relative to
// each other. Do not add the flip to the cascade matrices to "match" this
// function.
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

// Packs every GUI cloud slider into SceneUBO's four cloud vec4s. The unpack
// side is clouds_main's cloud-parameter unpack block in clouds.slang
// (BuildIntegrity.CloudUboPackingMatchesTheShaderUnpack pins the two against
// each other):
//   cloudLightMarch   x = numMarchStepsToLight,       y/z/w reserved (0)
//   cloudMeshScale    xyz = meshScale,                w = densityMultiplier (cloud.scale)
//   cloudMeshOffset   xyz = meshOffset,                w = coverageThreshold (cloud.threshold)
//   cloudParameters   x = pillowness, y = cirrusEffect, z = powderEffect, w = numMarchSteps
// Takes plain scalars, not GUISceneSharedVars: common/*.hpp are included in
// the global module fragment and cannot name a module-exported type (this is
// why clampPcfRadius takes an int).
inline void fillSceneUboClouds(VulkanRendererInternals::SceneUBO &ubo,
  glm::vec3 meshScale,
  float densityMultiplier,
  glm::vec3 meshOffset,
  float coverageThreshold,
  int numMarchSteps,
  int numMarchStepsToLight,
  float pillowness,
  float cirrusEffect,
  bool powderEffect)
{
    ubo.cloudLightMarch = glm::vec4(static_cast<float>(numMarchStepsToLight), 0.0F, 0.0F, 0.0F);
    ubo.cloudMeshScale = clampCloudMeshScale(meshScale, densityMultiplier);
    ubo.cloudMeshOffset = glm::vec4(meshOffset.x, meshOffset.y, meshOffset.z, coverageThreshold);
    ubo.cloudParameters = glm::vec4(
      pillowness, cirrusEffect, powderEffect ? 1.0F : 0.0F, static_cast<float>(numMarchSteps));
}

// Packs the camera position/direction into their two SceneUBO vec4s. Both
// .w components are filler (1.0F): every shader that reads either field
// samples only .xyz (calc_cascaded_shadow's fragDistance line in
// cascaded_shadow.slang, clouds_main's eyePosition read in clouds.slang,
// lighting_fs_main's V computation in deferred.slang, fs_main's V computation
// in rasterizer.slang, rchit_main's V computation in raytrace.rchit.slang).
// fov is not packed here - makeVulkanProjection already carries it into the
// projection matrix, and no shader has ever read it back out of cam_pos.w.
inline void fillSceneUboCamera(VulkanRendererInternals::SceneUBO &ubo, glm::vec3 position, glm::vec3 direction)
{
    ubo.view_dir = glm::vec4(direction, 1.0F);
    ubo.cam_pos = glm::vec4(position, 1.0F);
}

// Packs the directional light. Normalizes rawDirection itself so
// normalizedLightDirection has exactly one caller. direction.w is filler
// (1.0F, unread); color.w carries radiance and IS read - rasterizer.slang's
// fs_main unpacks it as lightIntensity - so unlike every other .w slot in
// SceneUBO, this one must stay wired to a shader (see
// BuildIntegrity.SceneUboWComponentsCarryingDataAreReadByAShader).
inline void fillSceneUboDirectionalLight(
  VulkanRendererInternals::SceneUBO &ubo, glm::vec3 rawDirection, glm::vec3 color, float radiance)
{
    ubo.dirLight.direction = glm::vec4(normalizedLightDirection(rawDirection), 1.0F);
    ubo.dirLight.color = glm::vec4(color, radiance);
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
