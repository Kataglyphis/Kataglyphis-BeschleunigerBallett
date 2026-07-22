module;
#include <memory>

#include "renderer/pushConstants/PushConstantPathTracing.hpp"
#include <glm/glm.hpp>
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

    void init(std::shared_ptr<VulkanDevice>in_device, const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts);

    void shaderHotReload(const std::vector<vk::DescriptorSetLayout> &descriptor_set_layouts);

    void recordCommands(vk::CommandBuffer &commandBuffer,
      uint32_t image_index,
      VulkanImage &vulkanImage,
      VulkanImage &accumulationImage,
      VulkanSwapChain *vulkanSwapChain,
      const std::vector<vk::DescriptorSet> &descriptorSets,
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
        uint32_t specWorkGroupSizeX = 16;
        uint32_t specWorkGroupSizeY = 8;
        uint32_t specWorkGroupSizeZ = 0;
    };

    SpecializationData specializationData;

    void createPipeline(const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts);
};
}// namespace Kataglyphis::VulkanRendererInternals
