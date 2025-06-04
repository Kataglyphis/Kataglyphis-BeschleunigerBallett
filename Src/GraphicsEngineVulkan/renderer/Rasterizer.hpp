#pragma once
#include <vulkan/vulkan.h>

#include "renderer/pushConstants/PushConstantRasterizer.hpp"
#include "scene/Scene.hpp"
#include "scene/Texture.hpp"
#include "vulkan_base/VulkanDevice.hpp"
#include "vulkan_base/VulkanSwapChain.hpp"

namespace KataglyphisRenderer::VulkanRendererInternals {
class Rasterizer
{
  public:
    Rasterizer();

    void init(VulkanDevice *device,
      VulkanSwapChain *vulkanSwapChain,
      const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts,
      VkCommandPool &commandPool);

    void shaderHotReload(const std::vector<VkDescriptorSetLayout> &descriptor_set_layouts);

    Texture &getOffscreenTexture(uint32_t index);

    void setPushConstant(PushConstantRasterizer pushConstant);

    void recordCommands(VkCommandBuffer &commandBuffer,
      uint32_t image_index,
      Scene *scene,
      const std::vector<VkDescriptorSet> &descriptorSets);

    void cleanUp();

    ~Rasterizer();

  private:
    VulkanDevice *device{ VK_NULL_HANDLE };
    VulkanSwapChain *vulkanSwapChain{ VK_NULL_HANDLE };

    CommandBufferManager commandBufferManager;

    std::vector<VkFramebuffer> framebuffer;
    std::vector<Texture> offscreenTextures;
    Texture depthBufferImage;

    VkPushConstantRange push_constant_range{ VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM, 0, 0 };
    PushConstantRasterizer pushConstant{ glm::mat4(1.f) };

    VkPipeline graphics_pipeline{ VK_NULL_HANDLE };
    VkPipelineLayout pipeline_layout{ VK_NULL_HANDLE };
    VkRenderPass render_pass{ VK_NULL_HANDLE };

    void createTextures(VkCommandPool &commandPool);
    void createGraphicsPipeline(const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts);
    void createRenderPass();
    void createFramebuffer();
    void createPushConstantRange();
};
}// namespace KataglyphisRenderer::VulkanRendererInternals