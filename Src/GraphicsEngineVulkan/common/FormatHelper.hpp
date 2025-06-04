#pragma once

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <vector>

#include "spdlog/spdlog.h"

namespace KataglyphisRenderer {
static VkFormat choose_supported_format(VkPhysicalDevice physical_device,
  const std::vector<VkFormat> &formats,
  VkImageTiling tiling,
  VkFormatFeatureFlags feature_flags)
{
    // loop through options and find compatible one
    for (VkFormat format : formats) {
        // get properties for give format on this device
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(physical_device, format, &properties);

        // depending on tiling choice, need to check for different bit flag
        if (tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & feature_flags) == feature_flags) {
            return format;

        } else if (tiling == VK_IMAGE_TILING_OPTIMAL
                   && (properties.optimalTilingFeatures & feature_flags) == feature_flags) {
            return format;
        }
    }

    spdlog::error("Failed to find supported format!");
}
}// namespace KataglyphisRenderer