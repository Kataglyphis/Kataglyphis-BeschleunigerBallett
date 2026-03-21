module;

#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.swapchain;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.window;

export namespace Kataglyphis {
class VulkanSwapChain
{
  public:
    VulkanSwapChain();

    void initVulkanContext(VulkanDevice *in_device, Kataglyphis::Frontend::Window *window, const vk::SurfaceKHR &surface);

    const vk::SwapchainKHR &getSwapChain() const { return swapchain; };
    uint32_t getNumberSwapChainImages() const { auto __tmp_sz = swap_chain_images.size(); return static_cast<uint32_t>(__tmp_sz); };
    const vk::Extent2D &getSwapChainExtent() const { return swap_chain_extent; };
    const vk::Format &getSwapChainFormat() const { return swap_chain_image_format; };
    Texture &getSwapChainImage(uint32_t index) { return swap_chain_images[index]; };

    void cleanUp();

    ~VulkanSwapChain();

  private:
    VulkanDevice *device{ nullptr };
    Kataglyphis::Frontend::Window *window{ nullptr };

    vk::SwapchainKHR swapchain{};

    std::vector<Texture> swap_chain_images;
    vk::Format swap_chain_image_format{ vk::Format::eB8G8R8A8Unorm };
    vk::Extent2D swap_chain_extent{ 0, 0 };

    static vk::SurfaceFormatKHR choose_best_surface_format(const std::vector<vk::SurfaceFormatKHR> &formats);
    static vk::PresentModeKHR choose_best_presentation_mode(const std::vector<vk::PresentModeKHR> &presentation_modes);
    vk::Extent2D choose_swap_extent(const vk::SurfaceCapabilitiesKHR &surface_capabilities);
};
}// namespace Kataglyphis
