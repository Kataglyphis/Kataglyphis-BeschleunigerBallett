module;

#include <vulkan/vulkan.h>

#include <vector>

export module kataglyphis.vulkan.swap_chain_details;

export namespace Kataglyphis::VulkanRendererInternals {
struct SwapChainDetails
{
    VkSurfaceCapabilitiesKHR surface_capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentation_mode;
};
}// namespace Kataglyphis::VulkanRendererInternals
