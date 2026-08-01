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

// Single shared preference order for every depth attachment/image in the
// engine. Stencil-free formats come first: stencil is never used anywhere
// (stencilTestEnable = VK_FALSE for every pipeline, every stencilLoadOp is
// eDontCare), so there is no reason to prefer a combined depth/stencil
// format over a plain depth one. Whatever this returns MUST be the format
// used for both the render pass attachment and the depth image/view that
// backs it - hard-coding a different format at either end silently
// decouples them.
inline vk::Format chooseDepthFormat(vk::PhysicalDevice physical_device)
{
    return choose_supported_format(physical_device,
      { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
      vk::ImageTiling::eOptimal,
      vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

// vkCmdBlitImage with VK_FILTER_LINEAR needs all three of these on the
// format's optimalTilingFeatures: SAMPLED_IMAGE_FILTER_LINEAR for the linear
// sample itself, BLIT_SRC on the mip level being read, and BLIT_DST on the
// mip level being written. Checking only the filter bit lets an incapable
// device pass the gate and proceed into an invalid blit.
constexpr bool supportsMipmapGeneration(vk::FormatFeatureFlags optimalTilingFeatures)
{
    constexpr vk::FormatFeatureFlags required = vk::FormatFeatureFlagBits::eSampledImageFilterLinear
                                                 | vk::FormatFeatureFlagBits::eBlitSrc
                                                 | vk::FormatFeatureFlagBits::eBlitDst;
    return (optimalTilingFeatures & required) == required;
}
}// namespace Kataglyphis