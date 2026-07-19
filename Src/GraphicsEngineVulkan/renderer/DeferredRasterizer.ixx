module;

#include "renderer/pushConstants/PushConstantRasterizer.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.deferred_rasterizer;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.swapchain;
import kataglyphis.vulkan.command_buffer_manager;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.scene;

export namespace Kataglyphis::VulkanRendererInternals {
class DeferredRasterizer
{
  public:
    DeferredRasterizer();

    DeferredRasterizer(const DeferredRasterizer &) = delete;
    DeferredRasterizer &operator=(const DeferredRasterizer &) = delete;

    void init(std::shared_ptr<VulkanDevice>in_device,
      VulkanSwapChain *swap_chain,
      const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts,
      vk::CommandPool &commandPool);

    void shaderHotReload(const std::vector<vk::DescriptorSetLayout> &descriptor_set_layouts);

    Kataglyphis::Texture &getOffscreenTexture(uint32_t index);
    vk::ImageView getGBufferPosition(uint32_t index) { return gBufferPositions[index]->getImageView(); }
    vk::ImageView getGBufferNormal(uint32_t index) { return gBufferNormals[index]->getImageView(); }
    vk::ImageView getGBufferAlbedo(uint32_t index) { return gBufferAlbedos[index]->getImageView(); }
    vk::ImageView getGBufferMaterial(uint32_t index) { return gBufferMaterials[index]->getImageView(); }
    vk::ImageView getDepthBufferImageView() { return depthBufferImage->getImageView(); }

    void setPushConstant(PushConstantRasterizer push_constant);

    void recordCommands(vk::CommandBuffer &commandBuffer,
      uint32_t image_index,
      Kataglyphis::Scene *scene,
      const std::vector<vk::DescriptorSet> &descriptorSets);

    void recreateFrameResources(vk::CommandPool commandPool);
    void destroyFramebuffers();

    void cleanUp();

    ~DeferredRasterizer();

  private:
    std::shared_ptr<VulkanDevice>device{ nullptr };
    VulkanSwapChain *vulkanSwapChain{ nullptr };

    CommandBufferManager commandBufferManager;

    std::vector<vk::Framebuffer> framebuffer;
    
    // The final color output (offscreen texture, consumed by PostStage)
    std::vector<std::unique_ptr<Kataglyphis::Texture>> offscreenTextures;
    
    // GBuffer attachments
    std::vector<std::unique_ptr<Kataglyphis::Texture>> gBufferPositions;
    std::vector<std::unique_ptr<Kataglyphis::Texture>> gBufferNormals;
    std::vector<std::unique_ptr<Kataglyphis::Texture>> gBufferAlbedos;
    std::vector<std::unique_ptr<Kataglyphis::Texture>> gBufferMaterials; // Metallic, Roughness, AO

    std::unique_ptr<Kataglyphis::Texture> depthBufferImage;

    vk::PushConstantRange push_constant_range{ vk::ShaderStageFlagBits::eAll, 0, 0 };
    PushConstantRasterizer pushConstant{ glm::mat4(1.f) };

    // Geometry Pass
    vk::Pipeline geometryPipeline{};
    vk::PipelineLayout geometryPipelineLayout{};
    
    // Lighting Pass
    vk::Pipeline lightingPipeline{};
    vk::PipelineLayout lightingPipelineLayout{};

    vk::RenderPass renderPass{};

    void createTextures(vk::CommandPool &commandPool);
    void createRenderPass();
    void createPipelines(const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts);
    void createFramebuffer();
    void createPushConstantRange();
};
}// namespace Kataglyphis::VulkanRendererInternals
