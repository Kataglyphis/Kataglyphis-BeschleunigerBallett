#pragma once
#include <vulkan/vulkan.h>

#include <vector>
namespace Kataglyphis::VulkanRendererInternals {
struct SwapChainDetails
{
    // surface properties, e.g. image size/extent
    VkSurfaceCapabilitiesKHR surface_capabilities;
    // surface image formats, e.g. RGBA and size of each color
    std::vector<VkSurfaceFormatKHR> formats;
    // how images should be presented to screen
    std::vector<VkPresentModeKHR> presentation_mode;
};
}// namespace Kataglyphis::VulkanRendererInternals