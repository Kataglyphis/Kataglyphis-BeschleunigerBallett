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

// True for every depth format in chooseDepthFormat's preference list (and the
// other combined depth/stencil formats Vulkan defines) that carries a stencil
// aspect alongside depth.
constexpr bool formatHasStencil(vk::Format format)
{
    return format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint
           || format == vk::Format::eD16UnormS8Uint || format == vk::Format::eS8Uint;
}

// Aspect mask for a layout transition (or attachment view) of a depth image:
// a combined depth/stencil image must name both aspects it owns, so the
// transition/attachment does not leave the stencil aspect in an undefined
// layout. This does NOT apply to every view of a depth image - a **sampled**
// view (combined image sampler) or an **input-attachment** view must name
// exactly one aspect (eDepth), because both are validated against a single
// aspect and stencil is never read back by either. Call sites that build such
// a view stay hard-coded to eDepth and point back here instead of calling
// this helper.
constexpr vk::ImageAspectFlags depthStencilTransitionAspect(vk::Format format)
{
    vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eDepth;
    if (formatHasStencil(format)) { aspect |= vk::ImageAspectFlagBits::eStencil; }
    return aspect;
}
}// namespace Kataglyphis