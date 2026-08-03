module;
#include <memory>

#include "renderer/SwapChainDetails.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "GLFW/glfw3.h"
#include "common/Utilities.hpp"
#include "vulkan_base/SwapchainChoices.hpp"

module kataglyphis.vulkan.swapchain;

import kataglyphis.vulkan.queue_family_indices;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.window;

Kataglyphis::VulkanSwapChain::VulkanSwapChain() = default;

void Kataglyphis::VulkanSwapChain::initVulkanContext(std::shared_ptr<VulkanDevice>in_device,
  Kataglyphis::Frontend::Window *frontend_window,
  const vk::SurfaceKHR &surface,
  const vk::SwapchainKHR &oldSwapchain)
{
    this->device = in_device;
    this->window = frontend_window;

    // get swap chain details so we can pick the best settings
    Kataglyphis::VulkanRendererInternals::SwapChainDetails const swap_chain_details = device->getSwapchainDetails();

    // 1. choose best surface format
    // 2. choose best presentation mode
    // 3. choose swap chain image resolution

    vk::SurfaceFormatKHR const surface_format = chooseBestSurfaceFormat(swap_chain_details.formats);
    vk::PresentModeKHR const present_mode = chooseBestPresentationMode(swap_chain_details.presentation_mode);

    // if current extent is at numeric limits, than extent can vary. Otherwise it
    // is size of window. This part needs a live window, so it stays here rather
    // than in the pure SwapchainChoices helpers.
    vk::Extent2D extent{};
    const vk::SurfaceCapabilitiesKHR &surface_capabilities = swap_chain_details.surface_capabilities;
    if (surface_capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        extent = surface_capabilities.currentExtent;
    } else {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window->get_window(), &width, &height);
        extent = clampSwapExtent(surface_capabilities, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    }

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
    // eColorAttachment (render target) and eTransferDst (blit destination) are
    // core to how the swapchain is used and are effectively universally
    // supported for present-capable surfaces, so they are requested
    // unconditionally.
    vk::ImageUsageFlags image_usage =
      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;

    // eSampled and eStorage are OPTIONAL swapchain usages: many present-capable
    // surfaces/drivers do not advertise them in supportedUsageFlags. Requesting
    // an unsupported usage makes createSwapchainKHR fail (-> ASSERT_VULKAN abort
    // below). Mask each against the reported capabilities, like eTransferSrc, so
    // the swapchain still creates on drivers that lack them.
    const vk::ImageUsageFlags supported_usage = swap_chain_details.surface_capabilities.supportedUsageFlags;
    if (supported_usage & vk::ImageUsageFlagBits::eSampled) { image_usage |= vk::ImageUsageFlagBits::eSampled; }
    if (supported_usage & vk::ImageUsageFlagBits::eStorage) { image_usage |= vk::ImageUsageFlagBits::eStorage; }

    // eTransferSrc is optional: it is only requested when the surface reports
    // support for it, so a surface without it still yields a valid swapchain -
    // frame capture then degrades to "unsupported" instead of failing creation.
    transfer_src_supported = static_cast<bool>(supported_usage & vk::ImageUsageFlagBits::eTransferSrc);
    if (transfer_src_supported) { image_usage |= vk::ImageUsageFlagBits::eTransferSrc; }

    swap_chain_create_info.imageUsage = image_usage;
    swap_chain_create_info.preTransform =
      swap_chain_details.surface_capabilities.currentTransform;// transform to perform on swap chain images
    swap_chain_create_info.compositeAlpha =
      vk::CompositeAlphaFlagBitsKHR::eOpaque;// dont do blending; everything opaque
    swap_chain_create_info.clipped = vk::True;// of course activate clipping ! :)

    // Declared at function scope, not inside the branch below: its storage
    // must outlive the createSwapchainKHR call at the bottom of this
    // function, which reads swap_chain_create_info.pQueueFamilyIndices long
    // after the branch's block would otherwise have ended.
    std::array<uint32_t, 2> queue_family_indices{};

    // if graphics and presentation families are different then swapchain must let
    // images be shared between families
    if (indices.graphics_family != indices.presentation_family) {
        queue_family_indices = { static_cast<uint32_t>(indices.graphics_family),
            static_cast<uint32_t>(indices.presentation_family) };

        swap_chain_create_info.imageSharingMode = vk::SharingMode::eConcurrent;// image share handling
        swap_chain_create_info.queueFamilyIndexCount = 2;// number of queues to share images between
        swap_chain_create_info.pQueueFamilyIndices = queue_family_indices.data();// array of queues to share between

    } else {
        swap_chain_create_info.imageSharingMode = vk::SharingMode::eExclusive;
        swap_chain_create_info.queueFamilyIndexCount = 0;
        swap_chain_create_info.pQueueFamilyIndices = nullptr;
    }

    // Hand the driver the outgoing swapchain so it can recycle its images
    // during recreation. Passing nullptr (the old behaviour) forced the
    // driver to allocate an entirely fresh swapchain while the previous one
    // had already been destroyed - a resize hitch and, on some drivers, a
    // transient allocation spike.
    swap_chain_create_info.oldSwapchain = oldSwapchain;

    // create swap chain
    vk::ResultValue<vk::SwapchainKHR> swapchain_result =
      device->getLogicalDevice().createSwapchainKHR(swap_chain_create_info);
    // The result was never checked: on eErrorOutOfDateKHR / eErrorSurfaceLostKHR
    // (both possible when a resize races the recreate) this stored a null
    // handle and every later use was UB. Fail fast instead.
    ASSERT_VULKAN(VkResult(swapchain_result.result), "Failed to (re)create swapchain!")
    swapchain = swapchain_result.value;

    // store for later reference
    swap_chain_image_format = surface_format.format;
    swap_chain_extent = extent;

    // get swapchain images
    vk::ResultValue<std::vector<vk::Image>> images_result = device->getLogicalDevice().getSwapchainImagesKHR(swapchain);
    ASSERT_VULKAN(VkResult(images_result.result), "Failed to get swapchain images!")
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

void Kataglyphis::VulkanSwapChain::destroyImageViews()
{
    // Each Texture owns the view VulkanImageView::create produced for it (the
    // wrapped swapchain image itself is not owned - setImage cleared
    // owns_image, so VulkanImage::cleanUp is a no-op there).
    for (Texture &image : swap_chain_images) { image.cleanUp(); }
    swap_chain_images.clear();
}

void Kataglyphis::VulkanSwapChain::cleanUp()
{
    if (!device) { return; }
    destroyImageViews();
    device->getLogicalDevice().destroySwapchainKHR(swapchain);
    swapchain = nullptr;
    device.reset();
}

Kataglyphis::VulkanSwapChain::~VulkanSwapChain() { cleanUp(); }

void Kataglyphis::VulkanSwapChain::recreate(std::shared_ptr<VulkanDevice>in_device, const vk::SurfaceKHR &surface)
{
    // Keep the old swapchain alive across the create so it can be the
    // oldSwapchain handoff source (the previous code destroyed everything
    // first and passed nullptr). The image views reference the OLD images and
    // must go before the new swapchain's are built; the swapchain HANDLE
    // survives until the new one is created, then is destroyed.
    const vk::SwapchainKHR previousSwapchain = swapchain;
    destroyImageViews();
    initVulkanContext(in_device, window, surface, previousSwapchain);
    if (previousSwapchain) { in_device->getLogicalDevice().destroySwapchainKHR(previousSwapchain); }
}
