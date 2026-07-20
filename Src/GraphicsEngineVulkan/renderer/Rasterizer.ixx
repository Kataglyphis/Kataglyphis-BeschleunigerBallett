module;
#include <optional>

#include "renderer/pushConstants/PushConstantRasterizer.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.rasterizer;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.swapchain;
import kataglyphis.vulkan.command_buffer_manager;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.frustum;

export namespace Kataglyphis::VulkanRendererInternals {
class Rasterizer
{
  public:
    Rasterizer();

    Rasterizer(const Rasterizer &) = delete;
    Rasterizer &operator=(const Rasterizer &) = delete;

    void init(std::shared_ptr<VulkanDevice>in_device,
      VulkanSwapChain *swap_chain,
      const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts,
      vk::CommandPool &commandPool);

    void shaderHotReload(const std::vector<vk::DescriptorSetLayout> &descriptor_set_layouts);

    Kataglyphis::Texture &getOffscreenTexture(uint32_t index);

    void setPushConstant(PushConstantRasterizer push_constant);

    /// `cameraFrustum` culls meshes whose world-space bounds fall entirely
    /// outside the view. Pass std::nullopt to draw everything - and note that
    /// the SHADOW pass must do exactly that: geometry behind or beside the
    /// camera still casts into view, so culling casters by the camera frustum
    /// deletes shadows rather than saving work.
    void recordCommands(vk::CommandBuffer &commandBuffer,
      uint32_t image_index,
      Kataglyphis::Scene *scene,
      const std::vector<vk::DescriptorSet> &descriptorSets,
      const std::optional<FrustumPlanes> &cameraFrustum = std::nullopt);

    /// Meshes drawn / considered by the most recent recordCommands call.
    /// Reset at the start of each call, so a frame that records nothing
    /// reports zeros rather than stale counts from the previous frame.
    unsigned int getMeshesDrawn() const { return meshesDrawn; }
    unsigned int getMeshesConsidered() const { return meshesConsidered; }

    void recreateFrameResources(vk::CommandPool commandPool);
    void destroyFramebuffers();

    void cleanUp();

    ~Rasterizer();

  private:
    unsigned int meshesDrawn{ 0 };
    unsigned int meshesConsidered{ 0 };
    std::shared_ptr<VulkanDevice>device{ nullptr };
    VulkanSwapChain *vulkanSwapChain{ nullptr };

    CommandBufferManager commandBufferManager;

    std::vector<vk::Framebuffer> framebuffer;
    std::vector<std::unique_ptr<Kataglyphis::Texture>> offscreenTextures;
    std::unique_ptr<Kataglyphis::Texture> depthBufferImage;

    vk::PushConstantRange push_constant_range{ vk::ShaderStageFlagBits::eAll, 0, 0 };
    PushConstantRasterizer pushConstant{ glm::mat4(1.f) };

    vk::Pipeline graphics_pipeline{};
    vk::PipelineLayout pipeline_layout{};
    vk::RenderPass render_pass{};

    void createTextures(vk::CommandPool &commandPool);
    void createGraphicsPipeline(const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts);
    void createRenderPass();
    void createFramebuffer();
    void createPushConstantRange();
};
}// namespace Kataglyphis::VulkanRendererInternals