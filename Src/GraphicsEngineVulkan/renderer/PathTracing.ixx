module;
#include <memory>

#include "renderer/pushConstants/PushConstantPathTracing.hpp"
#include <glm/glm.hpp>
#include <span>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.path_tracing;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.image;
import kataglyphis.vulkan.swapchain;

export namespace Kataglyphis::VulkanRendererInternals {
class PathTracing
{
  public:
    PathTracing();

    PathTracing(const PathTracing &) = delete;
    PathTracing &operator=(const PathTracing &) = delete;

    void init(std::shared_ptr<VulkanDevice>in_device, std::span<const vk::DescriptorSetLayout> descriptorSetLayouts);

    void shaderHotReload(std::span<const vk::DescriptorSetLayout> descriptor_set_layouts);

    void recordCommands(vk::CommandBuffer &commandBuffer,
      uint32_t image_index,
      VulkanImage &vulkanImage,
      VulkanImage &accumulationImage,
      VulkanSwapChain *vulkanSwapChain,
      std::span<const vk::DescriptorSet> descriptorSets,
      uint32_t frame_index,
      uint32_t samples_per_pixel,
      uint32_t max_bounces);

    void cleanUp();

    ~PathTracing();

  private:
    std::shared_ptr<VulkanDevice>device{ nullptr };

    vk::PipelineLayout pipeline_layout{};
    vk::Pipeline pipeline{};
    [[maybe_unused]] vk::PushConstantRange pc_range{ vk::ShaderStageFlagBits::eAll, 0, 0 };
    PushConstantPathTracing push_constant{ glm::vec4(0.f), 0, 0 };

    // NOTE: this stage used to own a private 2-query timestamp pool that was
    // written every frame but never read back. Per-pass GPU timing now lives
    // centrally in VulkanRenderer (one pool, per-swapchain-image slices, read
    // back a frame later); the renderer's "Main" pass brackets this stage, so
    // the private pool was removed instead of being wired in.

    struct
    {
        uint32_t maxComputeWorkGroupCount[3] = { 0, 0, 0 };
        uint32_t maxComputeWorkGroupInvocations = 0;
        uint32_t maxComputeWorkGroupSize[3] = { 0, 0, 0 };

    } computeLimits;

    struct SpecializationData
    {
        // Must match path_tracing.slang's [numthreads(8, 8, 1)] — Slang
        // hardcodes the local size rather than consuming these as SPIR-V
        // specialization constants, so this struct only feeds the dispatch
        // work-group-count math below. A mismatch under-dispatches: found
        // 2026-07-31 at (16, 8), which covered only the left half of the
        // image width every frame.
        uint32_t specWorkGroupSizeX = 8;
        uint32_t specWorkGroupSizeY = 8;
        uint32_t specWorkGroupSizeZ = 0;
    };

    SpecializationData specializationData;

    void createPipeline(std::span<const vk::DescriptorSetLayout> descriptorSetLayouts);
};
}// namespace Kataglyphis::VulkanRendererInternals
