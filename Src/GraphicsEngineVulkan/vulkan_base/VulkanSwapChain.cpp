#include "vulkan_base/VulkanSwapChain.hpp"

#include <limits>

#include "common/Utilities.hpp"

Kataglyphis::VulkanSwapChain::VulkanSwapChain() {}

void Kataglyphis::VulkanSwapChain::initVulkanContext(VulkanDevice *device,
  Kataglyphis::Frontend::Window *window,
  const VkSurfaceKHR &surface)
{
    this->device = device;
    this->window = window;

    // get swap chain details so we can pick the best settings
    Kataglyphis::VulkanRendererInternals::SwapChainDetails swap_chain_details = device->getSwapchainDetails();

    // 1. choose best surface format
    // 2. choose best presentation mode
    // 3. choose swap chain image resolution

    VkSurfaceFormatKHR surface_format = choose_best_surface_format(swap_chain_details.formats);
    VkPresentModeKHR present_mode = choose_best_presentation_mode(swap_chain_details.presentation_mode);
    VkExtent2D extent = choose_swap_extent(swap_chain_details.surface_capabilities);

    // how many images are in the swap chain; get 1 more than the minimum to allow
    // tiple buffering
    uint32_t image_count = swap_chain_details.surface_capabilities.minImageCount + 1;

    // if maxImageCount == 0, then limitless
    if (swap_chain_details.surface_capabilities.maxImageCount > 0
        && swap_chain_details.surface_capabilities.maxImageCount < image_count) {
        image_count = swap_chain_details.surface_capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swap_chain_create_info{};
    swap_chain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swap_chain_create_info.surface = surface;// swapchain surface
    swap_chain_create_info.imageFormat = surface_format.format;// swapchain format
    swap_chain_create_info.imageColorSpace = surface_format.colorSpace;// swapchain color space
    swap_chain_create_info.presentMode = present_mode;// swapchain presentation mode
    swap_chain_create_info.imageExtent = extent;// swapchain image extents
    swap_chain_create_info.minImageCount = image_count;// minimum images in swapchain
    swap_chain_create_info.imageArrayLayers = 1;// number of layers for each image in chain
    swap_chain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                                        | VK_IMAGE_USAGE_STORAGE_BIT
                                        | VK_IMAGE_USAGE_TRANSFER_DST_BIT;// what attachment images will be used
                                                                          // as
    swap_chain_create_info.preTransform =
      swap_chain_details.surface_capabilities.currentTransform;// transform to perform on swap chain images
    swap_chain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;// dont do blending; everything opaque
    swap_chain_create_info.clipped = VK_TRUE;// of course activate clipping ! :)

    // get queue family indices
    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices indices = device->getQueueFamilies();

    // if graphics and presentation families are different then swapchain must let
    // images be shared between families
    if (indices.graphics_family != indices.presentation_family) {
        uint32_t queue_family_indices[] = { (uint32_t)indices.graphics_family, (uint32_t)indices.presentation_family };

        swap_chain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;// image share handling
        swap_chain_create_info.queueFamilyIndexCount = 2;// number of queues to share images between
        swap_chain_create_info.pQueueFamilyIndices = queue_family_indices;// array of queues to share between

    } else {
        swap_chain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swap_chain_create_info.queueFamilyIndexCount = 0;
        swap_chain_create_info.pQueueFamilyIndices = nullptr;
    }

    // if old swap chain been destroyed and this one replaces it then link old one
    // to quickly hand over responsibilities
    swap_chain_create_info.oldSwapchain = VK_NULL_HANDLE;

    // create swap chain
    VkResult result = vkCreateSwapchainKHR(device->getLogicalDevice(), &swap_chain_create_info, nullptr, &swapchain);
    ASSERT_VULKAN(result, "Failed create swapchain!");

    // store for later reference
    swap_chain_image_format = surface_format.format;
    swap_chain_extent = extent;

    // get swapchain images (first count, then values)
    uint32_t swapchain_image_count;
    vkGetSwapchainImagesKHR(device->getLogicalDevice(), swapchain, &swapchain_image_count, nullptr);
    std::vector<VkImage> images(swapchain_image_count);
    vkGetSwapchainImagesKHR(device->getLogicalDevice(), swapchain, &swapchain_image_count, images.data());

    swap_chain_images.clear();

    for (size_t i = 0; i < images.size(); i++) {
        VkImage image = images[static_cast<uint32_t>(i)];
        // store image handle
        Texture swap_chain_image{};
        swap_chain_image.setImage(image);
        swap_chain_image.createImageView(device, swap_chain_image_format, VK_IMAGE_ASPECT_COLOR_BIT, 1);

        // add to swapchain image list
        swap_chain_images.push_back(swap_chain_image);
    }
}

void Kataglyphis::VulkanSwapChain::cleanUp()
{
    for (Texture &image : swap_chain_images) {
        vkDestroyImageView(device->getLogicalDevice(), image.getImageView(), nullptr);
    }

    vkDestroySwapchainKHR(device->getLogicalDevice(), swapchain, nullptr);
}

Kataglyphis::VulkanSwapChain::~VulkanSwapChain() {}

VkSurfaceFormatKHR Kataglyphis::VulkanSwapChain::choose_best_surface_format(
  const std::vector<VkSurfaceFormatKHR> &formats)
{
    // best format is subjective, but I go with:
    //  Format:           VK_FORMAT_R8G8B8A8_UNORM (backup-format:
    //  VK_FORMAT_B8G8R8A8_UNORM) color_space:  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
    //  the condition in if means all formats are available (no restrictions)
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
        return { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    }

    // if restricted, search  for optimal format
    for (const auto &format : formats) {
        if ((format.format == VK_FORMAT_R8G8B8A8_UNORM || format.format == VK_FORMAT_B8G8R8A8_UNORM)
            && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    // in case just return first one--- but really shouldn't be the case ....
    return formats[0];
}

VkPresentModeKHR Kataglyphis::VulkanSwapChain::choose_best_presentation_mode(
  const std::vector<VkPresentModeKHR> &presentation_modes)
{
    // look for mailbox presentation mode
    for (const auto &presentation_mode : presentation_modes) {
        if (presentation_mode == VK_PRESENT_MODE_MAILBOX_KHR) { return presentation_mode; }
    }

    // if can't find, use FIFO as Vulkan spec says it must be present
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Kataglyphis::VulkanSwapChain::choose_swap_extent(const VkSurfaceCapabilitiesKHR &surface_capabilities)
{
    // if current extent is at numeric limits, than extent can vary. Otherwise it
    // is size of window
    if (surface_capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return surface_capabilities.currentExtent;

    } else {
        int width, height;
        glfwGetFramebufferSize(window->get_window(), &width, &height);

        // create new extent using window size
        VkExtent2D new_extent{};
        new_extent.width = static_cast<uint32_t>(width);
        new_extent.height = static_cast<uint32_t>(height);

        // surface also defines max and min, so make sure within boundaries bly
        // clamping value
        new_extent.width = std::max(surface_capabilities.minImageExtent.width,
          std::min(surface_capabilities.maxImageExtent.width, new_extent.width));
        new_extent.height = std::max(surface_capabilities.minImageExtent.height,
          std::min(surface_capabilities.maxImageExtent.height, new_extent.height));

        return new_extent;
    }
}
