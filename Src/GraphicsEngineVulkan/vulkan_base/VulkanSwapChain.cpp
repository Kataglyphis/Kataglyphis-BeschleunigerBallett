module;
#include <memory>

#include "renderer/SwapChainDetails.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "GLFW/glfw3.h"
#include "common/Utilities.hpp"

module kataglyphis.vulkan.swapchain;

import kataglyphis.vulkan.queue_family_indices;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.window;

Kataglyphis::VulkanSwapChain::VulkanSwapChain() = default;

void Kataglyphis::VulkanSwapChain::initVulkanContext(std::shared_ptr<VulkanDevice>in_device,
  Kataglyphis::Frontend::Window *frontend_window,
  const vk::SurfaceKHR &surface)
{
    this->device = in_device;
    this->window = frontend_window;

    // get swap chain details so we can pick the best settings
    Kataglyphis::VulkanRendererInternals::SwapChainDetails const swap_chain_details = device->getSwapchainDetails();

    // 1. choose best surface format
    // 2. choose best presentation mode
    // 3. choose swap chain image resolution

    vk::SurfaceFormatKHR const surface_format = choose_best_surface_format(swap_chain_details.formats);
    vk::PresentModeKHR const present_mode = choose_best_presentation_mode(swap_chain_details.presentation_mode);
    vk::Extent2D const extent = choose_swap_extent(swap_chain_details.surface_capabilities);

    // how many images are in the swap chain; get 1 more than the minimum to allow
    // tiple buffering
    uint32_t image_count = swap_chain_details.surface_capabilities.minImageCount + 1;

    // if maxImageCount == 0, then limitless
    if (swap_chain_details.surface_capabilities.maxImageCount > 0
        && swap_chain_details.surface_capabilities.maxImageCount < image_count) {
        image_count = swap_chain_details.surface_capabilities.maxImageCount;
    }

    // get queue family indices
    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices const indices = device->getQueueFamilies();

    vk::SwapchainCreateInfoKHR swap_chain_create_info{};
    swap_chain_create_info.surface = surface;// swapchain surface
    swap_chain_create_info.imageFormat = surface_format.format;// swapchain format
    swap_chain_create_info.imageColorSpace = surface_format.colorSpace;// swapchain color space
    swap_chain_create_info.presentMode = present_mode;// swapchain presentation mode
    swap_chain_create_info.imageExtent = extent;// swapchain image extents
    swap_chain_create_info.minImageCount = image_count;// minimum images in swapchain
    swap_chain_create_info.imageArrayLayers = 1;// number of layers for each image in chain
    swap_chain_create_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled
                                        | vk::ImageUsageFlagBits::eStorage
                                        | vk::ImageUsageFlagBits::eTransferDst;// what attachment images will be used
                                                                               // as
    swap_chain_create_info.preTransform =
      swap_chain_details.surface_capabilities.currentTransform;// transform to perform on swap chain images
    swap_chain_create_info.compositeAlpha =
      vk::CompositeAlphaFlagBitsKHR::eOpaque;// dont do blending; everything opaque
    swap_chain_create_info.clipped = vk::True;// of course activate clipping ! :)

    // if graphics and presentation families are different then swapchain must let
    // images be shared between families
    if (indices.graphics_family != indices.presentation_family) {
        uint32_t queue_family_indices[] = { static_cast<uint32_t>(indices.graphics_family),
            static_cast<uint32_t>(indices.presentation_family) };

        swap_chain_create_info.imageSharingMode = vk::SharingMode::eConcurrent;// image share handling
        swap_chain_create_info.queueFamilyIndexCount = 2;// number of queues to share images between
        swap_chain_create_info.pQueueFamilyIndices = queue_family_indices;// array of queues to share between

    } else {
        swap_chain_create_info.imageSharingMode = vk::SharingMode::eExclusive;
        swap_chain_create_info.queueFamilyIndexCount = 0;
        swap_chain_create_info.pQueueFamilyIndices = nullptr;
    }

    // if old swap chain been destroyed and this one replaces it then link old one
    // to quickly hand over responsibilities
    swap_chain_create_info.oldSwapchain = nullptr;

    // create swap chain
    vk::ResultValue<vk::SwapchainKHR> swapchain_result =
      device->getLogicalDevice().createSwapchainKHR(swap_chain_create_info);
    swapchain = swapchain_result.value;

    // store for later reference
    swap_chain_image_format = surface_format.format;
    swap_chain_extent = extent;

    // get swapchain images
    vk::ResultValue<std::vector<vk::Image>> images_result = device->getLogicalDevice().getSwapchainImagesKHR(swapchain);
    std::vector<vk::Image> images = images_result.value;

    swap_chain_images.clear();

    for (size_t i = 0; i < images.size(); i++) {
        vk::Image image = images[static_cast<uint32_t>(i)];
        swap_chain_images.emplace_back();
        Texture &swap_chain_image = swap_chain_images.back();
        swap_chain_image.setImage(image);
        swap_chain_image.createImageView(device, swap_chain_image_format, vk::ImageAspectFlagBits::eColor, 1);
    }
}

void Kataglyphis::VulkanSwapChain::cleanUp()
{
    for (Texture &image : swap_chain_images) {
        device->getLogicalDevice().destroyImageView(image.getImageView());
        image.setImageView(vk::ImageView{});
        image.setImage(vk::Image{});
    }
    swap_chain_images.clear();

    device->getLogicalDevice().destroySwapchainKHR(swapchain);
}

Kataglyphis::VulkanSwapChain::~VulkanSwapChain() = default;

void Kataglyphis::VulkanSwapChain::recreate(std::shared_ptr<VulkanDevice>in_device, const vk::SurfaceKHR &surface)
{
    cleanUp();
    initVulkanContext(in_device, window, surface);
}

auto Kataglyphis::VulkanSwapChain::choose_best_surface_format(const std::vector<vk::SurfaceFormatKHR> &formats)
  -> vk::SurfaceFormatKHR
{
    // best format is subjective, but I go with:
    //  Format:           vk::Format::eR8G8B8A8Unorm (backup-format:
    //  vk::Format::eB8G8R8A8Unorm) color_space:  vk::ColorSpaceKHR::eSrgbNonlinear
    //  the condition in if means all formats are available (no restrictions)
    if (formats.size() == 1 && formats[0].format == vk::Format::eUndefined) {
        return { vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear };
    }

    // if restricted, search  for optimal format
    for (const auto &format : formats) {
        if ((format.format == vk::Format::eR8G8B8A8Unorm || format.format == vk::Format::eB8G8R8A8Unorm)
            && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return format;
        }
    }

    // in case just return first one--- but really shouldn't be the case ....
    return formats[0];
}

auto Kataglyphis::VulkanSwapChain::choose_best_presentation_mode(
  const std::vector<vk::PresentModeKHR> &presentation_modes) -> vk::PresentModeKHR
{
    // look for mailbox presentation mode
    for (const auto &presentation_mode : presentation_modes) {
        if (presentation_mode == vk::PresentModeKHR::eMailbox) { return presentation_mode; }
    }

    // if can't find, use FIFO as Vulkan spec says it must be present
    return vk::PresentModeKHR::eFifo;
}

auto Kataglyphis::VulkanSwapChain::choose_swap_extent(const vk::SurfaceCapabilitiesKHR &surface_capabilities)
  -> vk::Extent2D
{
    // if current extent is at numeric limits, than extent can vary. Otherwise it
    // is size of window
    if (surface_capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return surface_capabilities.currentExtent;
    }
    int width, height;
    glfwGetFramebufferSize(window->get_window(), &width, &height);

    // create new extent using window size
    vk::Extent2D new_extent{};
    new_extent.width = static_cast<uint32_t>(width);
    new_extent.height = static_cast<uint32_t>(height);

    // surface also defines max and min, so make sure within boundaries bly
    // clamping value
    uint32_t minWidth = surface_capabilities.minImageExtent.width;
    uint32_t maxWidth = surface_capabilities.maxImageExtent.width;
    uint32_t minHeight = surface_capabilities.minImageExtent.height;
    uint32_t maxHeight = surface_capabilities.maxImageExtent.height;

    new_extent.width = std::max(minWidth, std::min(maxWidth, new_extent.width));
    new_extent.height = std::max(minHeight, std::min(maxHeight, new_extent.height));

    return new_extent;
}
