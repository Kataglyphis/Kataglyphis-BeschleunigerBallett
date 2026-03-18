module;

#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.post_stage;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.swapchain;
import kataglyphis.vulkan.texture;

export namespace Kataglyphis::VulkanRendererInternals {
class PostStage
{
  public:
    PostStage();

    void init(VulkanDevice *in_device,
      VulkanSwapChain *vulkanSwapChain,
      const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts);

    void shaderHotReload(const std::vector<vk::DescriptorSetLayout> &descriptor_set_layouts);

    vk::RenderPass &getRenderPass() { return render_pass; };
    vk::Sampler &getOffscreenSampler() { return offscreenTextureSampler; };

    void recordCommands(vk::CommandBuffer &commandBuffer,
      uint32_t image_index,
      const std::vector<vk::DescriptorSet> &descriptorSets);
    void cleanUp();

    ~PostStage();

  private:
    VulkanDevice *device{ nullptr };
    VulkanSwapChain *vulkanSwapChain{ nullptr };

    std::vector<vk::Framebuffer> framebuffers;
    std::unique_ptr<Kataglyphis::Texture> depthBufferImage;
    vk::Format depth_format{ vk::Format::eUndefined };
    void createDepthbufferImage();

    vk::Sampler offscreenTextureSampler{};
    void createOffscreenTextureSampler();

    vk::PushConstantRange push_constant_range{ vk::ShaderStageFlagBits::eAll, 0, 0 };
    vk::RenderPass render_pass{};
    vk::Pipeline graphics_pipeline{};
    vk::PipelineLayout pipeline_layout{};

    void createPushConstantRange();
    void createRenderpass();
    void createGraphicsPipeline(const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts);
    void createFramebuffer();
};
}// namespace Kataglyphis::VulkanRendererInternals