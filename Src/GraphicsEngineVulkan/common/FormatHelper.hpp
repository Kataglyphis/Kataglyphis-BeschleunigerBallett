#pragma once

#include <vulkan/vulkan.hpp>

#include <stdexcept>
#include <vector>

#include "spdlog/spdlog.h"

namespace Kataglyphis {
static vk::Format choose_supported_format(vk::PhysicalDevice physical_device,
  const std::vector<vk::Format> &formats,
  vk::ImageTiling tiling,
  vk::FormatFeatureFlags feature_flags)
{
    // loop through options and find compatible one
    for (vk::Format format : formats) {
        // get properties for give format on this device
        vk::FormatProperties properties = physical_device.getFormatProperties(format);

        // depending on tiling choice, need to check for different bit flag
        if (tiling == vk::ImageTiling::eLinear && (properties.linearTilingFeatures & feature_flags) == feature_flags) {
            return format;

        } else if (tiling == vk::ImageTiling::eOptimal
                   && (properties.optimalTilingFeatures & feature_flags) == feature_flags) {
            return format;
        }
    }

    spdlog::error("Failed to find supported format!");
    return vk::Format::eUndefined;
}
}// namespace Kataglyphis