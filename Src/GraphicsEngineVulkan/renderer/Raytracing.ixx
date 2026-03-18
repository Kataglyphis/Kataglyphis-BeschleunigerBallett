module;

#include "renderer/pushConstants/PushConstantRayTracing.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.raytracing;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.image;
import kataglyphis.vulkan.swapchain;

export namespace Kataglyphis::VulkanRendererInternals {
class Raytracing
{
  public:
    Raytracing();

    void init(VulkanDevice *in_device, const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts);

    void shaderHotReload(const std::vector<vk::DescriptorSetLayout> &descriptor_set_layouts);

    void recordCommands(vk::CommandBuffer &commandBuffer,
      VulkanImage &renderImage,
      VulkanSwapChain *vulkanSwapChain,
      const std::vector<vk::DescriptorSet> &descriptorSets);

    void cleanUp();

    ~Raytracing();

  private:
    VulkanDevice *device{ nullptr };
    [[maybe_unused]] VulkanSwapChain *vulkanSwapChain{ nullptr };

    vk::Pipeline graphicsPipeline{};
    vk::PipelineLayout pipeline_layout{};
    PushConstantRaytracing pc{ glm::vec4(0.f) };
    vk::PushConstantRange pc_ranges{ vk::ShaderStageFlagBits::eAll, 0, 0 };

    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> shader_groups;
    VulkanBuffer shaderBindingTableBuffer;
    VulkanBuffer raygenShaderBindingTableBuffer;
    VulkanBuffer missShaderBindingTableBuffer;
    VulkanBuffer hitShaderBindingTableBuffer;

    vk::StridedDeviceAddressRegionKHR rgen_region{};
    vk::StridedDeviceAddressRegionKHR miss_region{};
    vk::StridedDeviceAddressRegionKHR hit_region{};
    vk::StridedDeviceAddressRegionKHR call_region{};

    vk::PhysicalDeviceRayTracingPipelinePropertiesKHR raytracing_properties{};

    void createPCRange();
    void createGraphicsPipeline(const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts);
    void createSBT();
};
}// namespace Kataglyphis::VulkanRendererInternals