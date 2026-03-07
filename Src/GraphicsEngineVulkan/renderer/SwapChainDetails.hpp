#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

namespace Kataglyphis::VulkanRendererInternals {
struct SwapChainDetails
{
    VkSurfaceCapabilitiesKHR surface_capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentation_mode;
};
}// namespace Kataglyphis::VulkanRendererInternals
