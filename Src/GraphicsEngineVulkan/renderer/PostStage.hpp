#pragma once

#include "vulkan_base/VulkanDevice.hpp"
#include "vulkan_base/VulkanSwapChain.hpp"

#include <vulkan/vulkan.h>

class PostStage
{
  public:
    PostStage();

    void init(VulkanDevice *device,
      VulkanSwapChain *vulkanSwapChain,
      const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts);

    void shaderHotReload(const std::vector<VkDescriptorSetLayout> &descriptor_set_layouts);

    VkRenderPass &getRenderPass() { return render_pass; };
    VkSampler &getOffscreenSampler() { return offscreenTextureSampler; };

    void recordCommands(VkCommandBuffer &commandBuffer,
      uint32_t image_index,
      const std::vector<VkDescriptorSet> &descriptorSets);
    void cleanUp();

    ~PostStage();

  private:
    VulkanDevice *device{ VK_NULL_HANDLE };
    VulkanSwapChain *vulkanSwapChain{ VK_NULL_HANDLE };

    std::vector<VkFramebuffer> framebuffers;
    Texture depthBufferImage;
    VkFormat depth_format{ VK_FORMAT_UNDEFINED };
    void createDepthbufferImage();

    VkSampler offscreenTextureSampler;
    void createOffscreenTextureSampler();

    VkPushConstantRange push_constant_range{ VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM, 0, 0 };
    VkRenderPass render_pass{ VK_NULL_HANDLE };
    VkPipeline graphics_pipeline{ VK_NULL_HANDLE };
    VkPipelineLayout pipeline_layout{ VK_NULL_HANDLE };

    void createPushConstantRange();
    void createRenderpass();
    void createGraphicsPipeline(const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts);
    void createFramebuffer();
};
