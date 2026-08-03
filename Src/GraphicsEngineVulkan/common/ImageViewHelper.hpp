#pragma once

#include <vulkan/vulkan.hpp>

namespace Kataglyphis {

// Every image view in this engine was built from the same six field
// assignments, by hand, in VulkanImageView::create and
// CascadedShadowMap::createFramebuffers - the second copy dropped the
// component swizzles (leaving them at the ImageViewCreateInfo default, which
// happens to also be eIdentity, but silently) and hard-coded levelCount = 1
// instead of taking it as a parameter.
//
// components is deliberately baked in as all-eIdentity rather than taken as
// a parameter - no call site in this engine remaps channels - so a caller
// that needs a swizzle builds its info inline and says why, the same rule
// FramebufferHelper.hpp, RenderPassHelper.hpp and ViewportHelper.hpp state,
// for the same reason.
//
// Built via the fully-explicit vk::ImageViewCreateInfo constructor rather
// than value-init-then-assign, for the same constexpr reason
// FramebufferHelper.hpp documents.
constexpr vk::ImageViewCreateInfo buildImageViewCreateInfo(vk::Image image,
  vk::Format format,
  vk::ImageAspectFlags aspect_flags,
  uint32_t mip_levels,
  vk::ImageViewType view_type = vk::ImageViewType::e2D,
  uint32_t array_layers = 1)
{
    return vk::ImageViewCreateInfo{ vk::ImageViewCreateFlags{}, image, view_type, format, vk::ComponentMapping{},
        vk::ImageSubresourceRange{ aspect_flags, 0, mip_levels, 0, array_layers } };
}

}// namespace Kataglyphis
