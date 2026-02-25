module;

#include "renderer/pushConstants/PushConstantRasterizer.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

export module kataglyphis.vulkan.rasterizer;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.swapchain;
import kataglyphis.vulkan.command_buffer_manager;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.scene;

export namespace Kataglyphis::VulkanRendererInternals {
class Rasterizer
{
  public:
    Rasterizer();

    void init(VulkanDevice *device,
      VulkanSwapChain *swap_chain,
      const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts,
      VkCommandPool &commandPool);

    void shaderHotReload(const std::vector<VkDescriptorSetLayout> &descriptor_set_layouts);

    Kataglyphis::Texture &getOffscreenTexture(uint32_t index);

    void setPushConstant(PushConstantRasterizer push_constant);

    void recordCommands(VkCommandBuffer &commandBuffer,
      uint32_t image_index,
      Kataglyphis::Scene *scene,
      const std::vector<VkDescriptorSet> &descriptorSets);

    void cleanUp();

    ~Rasterizer();

  private:
    VulkanDevice *device{ VK_NULL_HANDLE };
    VulkanSwapChain *vulkanSwapChain{ VK_NULL_HANDLE };

    CommandBufferManager commandBufferManager;

    std::vector<VkFramebuffer> framebuffer;
    std::vector<std::unique_ptr<Kataglyphis::Texture>> offscreenTextures;
    std::unique_ptr<Kataglyphis::Texture> depthBufferImage;

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
}// namespace Kataglyphis::VulkanRendererInternals