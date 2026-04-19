module;
#include <vector>
#include <memory>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.omni_dir_shadow_map;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.texture;

export namespace Kataglyphis {
class OmniDirShadowMap
{
  public:
    OmniDirShadowMap() = default;

    void init(VulkanDevice *device, uint32_t width, uint32_t height);
    
    Kataglyphis::Texture* getShadowMapCube() { return shadowMapCube; }
    vk::Framebuffer getFramebuffer() const { return framebuffer; }
    vk::RenderPass getRenderPass() const { return renderPass; }

    uint32_t getWidth() const { return shadowWidth; }
    uint32_t getHeight() const { return shadowHeight; }

    void cleanUp();
    ~OmniDirShadowMap() = default;

  private:
    VulkanDevice *device{ nullptr };
    uint32_t shadowWidth{ 0 };
    uint32_t shadowHeight{ 0 };

    Kataglyphis::Texture* shadowMapCube{nullptr};
    vk::RenderPass renderPass;
    vk::Framebuffer framebuffer;

    void createRenderPass();
    void createFramebuffers();
};
}
