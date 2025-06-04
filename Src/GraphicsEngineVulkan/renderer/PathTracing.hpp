#pragma once

#include <vulkan/vulkan.h>

#include "renderer/pushConstants/PushConstantPathTracing.hpp"
#include "vulkan_base/VulkanDevice.hpp"
#include "vulkan_base/VulkanSwapChain.hpp"

namespace Kataglyphis::VulkanRendererInternals {
class PathTracing
{
  public:
    PathTracing();

    void init(VulkanDevice *device, const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts);

    void shaderHotReload(const std::vector<VkDescriptorSetLayout> &descriptor_set_layouts);

    void recordCommands(VkCommandBuffer &commandBuffer,
      uint32_t image_index,
      VulkanImage &vulkanImage,
      VulkanSwapChain *vulkanSwapChain,
      const std::vector<VkDescriptorSet> &descriptorSets);

    void cleanUp();

    ~PathTracing();

  private:
    VulkanDevice *device{ VK_NULL_HANDLE };

    VkPipelineLayout pipeline_layout{ VK_NULL_HANDLE };
    VkPipeline pipeline{ VK_NULL_HANDLE };
    VkPushConstantRange pc_range{ VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM, 0, 0 };
    PushConstantPathTracing push_constant{ glm::vec4(0.f), 0, 0 };

    float timeStampPeriod{ 0 };
    uint64_t pathTracingTiming{ static_cast<uint64_t>(-1.f) };
    uint32_t query_count{ 2 };
    std::vector<uint64_t> queryResults;
    VkQueryPool queryPool{ VK_NULL_HANDLE };

    struct
    {
        uint32_t maxComputeWorkGroupCount[3] = { static_cast<uint32_t>(-1),
            static_cast<uint32_t>(-1),
            static_cast<uint32_t>(-1) };
        uint32_t maxComputeWorkGroupInvocations = -1;
        uint32_t maxComputeWorkGroupSize[3] = { static_cast<uint32_t>(-1),
            static_cast<uint32_t>(-1),
            static_cast<uint32_t>(-1) };

    } computeLimits;

    struct SpecializationData
    {
        // standard values
        uint32_t specWorkGroupSizeX = 16;
        uint32_t specWorkGroupSizeY = 8;
        uint32_t specWorkGroupSizeZ = 0;
    };

    SpecializationData specializationData;

    void createQueryPool();
    void createPipeline(const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts);
};
}// namespace Kataglyphis::VulkanRendererInternals