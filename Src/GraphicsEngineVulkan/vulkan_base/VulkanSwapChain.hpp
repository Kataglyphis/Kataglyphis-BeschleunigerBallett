#pragma once

#include "scene/Texture.hpp"

#include "vulkan_base/VulkanDevice.hpp"
#include "window/Window.hpp"

namespace Kataglyphis {
class VulkanSwapChain
{
  public:
    VulkanSwapChain();

    void initVulkanContext(VulkanDevice *device, Kataglyphis::Frontend::Window *window, const VkSurfaceKHR &surface);

    const VkSwapchainKHR &getSwapChain() const { return swapchain; };
    uint32_t getNumberSwapChainImages() const { return static_cast<uint32_t>(swap_chain_images.size()); };
    const VkExtent2D &getSwapChainExtent() const { return swap_chain_extent; };
    const VkFormat &getSwapChainFormat() const { return swap_chain_image_format; };
    Texture &getSwapChainImage(uint32_t index) { return swap_chain_images[index]; };

    void cleanUp();

    ~VulkanSwapChain();

  private:
    VulkanDevice *device{ VK_NULL_HANDLE };
    Kataglyphis::Frontend::Window *window{ VK_NULL_HANDLE };

    VkSwapchainKHR swapchain{ VK_NULL_HANDLE };

    std::vector<Texture> swap_chain_images;
    VkFormat swap_chain_image_format{ VK_FORMAT_B8G8R8A8_UNORM };
    VkExtent2D swap_chain_extent{ 0, 0 };

    VkSurfaceFormatKHR choose_best_surface_format(const std::vector<VkSurfaceFormatKHR> &formats);
    VkPresentModeKHR choose_best_presentation_mode(const std::vector<VkPresentModeKHR> &presentation_modes);
    VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR &surface_capabilities);
};
}// namespace Kataglyphis