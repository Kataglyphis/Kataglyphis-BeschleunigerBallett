module;
#include <optional>
#include <cstdint>
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "renderer/pushConstants/PushConstantRasterizer.hpp"

export module kataglyphis.vulkan.mesh_draw_recorder;

import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.frustum;

export namespace Kataglyphis::VulkanRendererInternals {

/// Meshes drawn vs considered by one recordSceneMeshDraws call, for the
/// caller's frame-timing/HUD counters.
struct MeshDrawStats
{
    unsigned int drawn;
    unsigned int considered;
};

/// Shared per-mesh draw-record loop, extracted verbatim from Rasterizer and
/// DeferredRasterizer (they carried an identical ~45-line copy each, already
/// drifting in their comments). `pipelineLayout` and `pushConstantStages` are
/// the two genuine differences between the callers - forward pushes
/// eVertex|eFragment through pipeline_layout, deferred pushes eAll through
/// geometryPipelineLayout - and must NOT be collapsed to one value.
/// `pushConstant` is taken by reference so the caller's already-seeded fields
/// (set via setPushConstant) survive; .model / .invModelRows / .objectIndex
/// are (re)written per mesh exactly as the inline loops did.
MeshDrawStats recordSceneMeshDraws(vk::CommandBuffer commandBuffer,
  vk::PipelineLayout pipelineLayout,
  vk::ShaderStageFlags pushConstantStages,
  Kataglyphis::Scene *scene,
  const std::optional<FrustumPlanes> &cameraFrustum,
  PushConstantRasterizer &pushConstant);

}// namespace Kataglyphis::VulkanRendererInternals
