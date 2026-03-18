#pragma once

#include <vector>
#include <vulkan/vulkan.hpp>

namespace Kataglyphis::VulkanRendererInternals {
struct SwapChainDetails
{
    vk::SurfaceCapabilitiesKHR surface_capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentation_mode;
};
}// namespace Kataglyphis::VulkanRendererInternals
