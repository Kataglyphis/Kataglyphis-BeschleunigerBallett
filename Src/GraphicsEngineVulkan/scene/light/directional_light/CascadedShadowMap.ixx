module;
#include <vector>
#include <memory>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.cascaded_shadow_map;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.texture;

export namespace Kataglyphis {
class CascadedShadowMap
{
  public:
    CascadedShadowMap() = default;

    void init(VulkanDevice *device, uint32_t width, uint32_t height, uint32_t num_cascades);
    
    Kataglyphis::Texture* getShadowMapArray() { return shadowMapArray.get(); }
    std::vector<vk::Framebuffer>& getFramebuffers() { return framebuffers; }
    vk::RenderPass getRenderPass() const { return renderPass; }

    uint32_t getWidth() const { return shadowWidth; }
    uint32_t getHeight() const { return shadowHeight; }
    uint32_t getNumCascades() const { return numCascades; }

    void cleanUp();
    ~CascadedShadowMap() = default;

  private:
    VulkanDevice *device{ nullptr };
    uint32_t shadowWidth{ 0 };
    uint32_t shadowHeight{ 0 };
    uint32_t numCascades{ 0 };

    std::unique_ptr<Kataglyphis::Texture> shadowMapArray;
    vk::RenderPass renderPass;
    std::vector<vk::Framebuffer> framebuffers;

    void createRenderPass();
    void createFramebuffers();
};
}