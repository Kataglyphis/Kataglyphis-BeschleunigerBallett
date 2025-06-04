#pragma once

#include <vulkan/vulkan.h>

#include "renderer/pushConstants/PushConstantRayTracing.hpp"
#include "vulkan_base/VulkanBuffer.hpp"
#include "vulkan_base/VulkanSwapChain.hpp"

namespace KataglyphisRenderer::VulkanRendererInternals {
class Raytracing
{
  public:
    Raytracing();

    void init(VulkanDevice *device, const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts);

    void shaderHotReload(const std::vector<VkDescriptorSetLayout> &descriptor_set_layouts);

    void recordCommands(VkCommandBuffer &commandBuffer,
      VulkanSwapChain *vulkanSwapChain,
      const std::vector<VkDescriptorSet> &descriptorSets);

    void cleanUp();

    ~Raytracing();

  private:
    VulkanDevice *device{ VK_NULL_HANDLE };
    VulkanSwapChain *vulkanSwapChain{ VK_NULL_HANDLE };

    VkPipeline graphicsPipeline{ VK_NULL_HANDLE };
    VkPipelineLayout pipeline_layout{ VK_NULL_HANDLE };
    PushConstantRaytracing pc{ glm::vec4(0.f) };
    VkPushConstantRange pc_ranges{ VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM, 0, 0 };

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;
    VulkanBuffer shaderBindingTableBuffer;
    VulkanBuffer raygenShaderBindingTableBuffer;
    VulkanBuffer missShaderBindingTableBuffer;
    VulkanBuffer hitShaderBindingTableBuffer;

    VkStridedDeviceAddressRegionKHR rgen_region{};
    VkStridedDeviceAddressRegionKHR miss_region{};
    VkStridedDeviceAddressRegionKHR hit_region{};
    VkStridedDeviceAddressRegionKHR call_region{};

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR raytracing_properties{};

    void createPCRange();
    void createGraphicsPipeline(const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts);
    void createSBT();
};
}// namespace KataglyphisRenderer::VulkanRendererInternals