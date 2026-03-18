module;

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

    void init(VulkanDevice *in_device, const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts);

    void shaderHotReload(const std::vector<vk::DescriptorSetLayout> &descriptor_set_layouts);

    void recordCommands(vk::CommandBuffer &commandBuffer,
      uint32_t image_index,
      VulkanImage &vulkanImage,
      VulkanSwapChain *vulkanSwapChain,
      const std::vector<vk::DescriptorSet> &descriptorSets);

    void cleanUp();

    ~PathTracing();

  private:
    VulkanDevice *device{ nullptr };

    vk::PipelineLayout pipeline_layout{};
    vk::Pipeline pipeline{};
    [[maybe_unused]] vk::PushConstantRange pc_range{ vk::ShaderStageFlagBits::eAll, 0, 0 };
    PushConstantPathTracing push_constant{ glm::vec4(0.f), 0, 0 };

    float timeStampPeriod{ 0 };
    [[maybe_unused]] uint64_t pathTracingTiming{ 0 };
    uint32_t query_count{ 2 };
    vk::QueryPool queryPool{};

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

    void createQueryPool();
    void createPipeline(const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts);
};
}// namespace Kataglyphis::VulkanRendererInternals
