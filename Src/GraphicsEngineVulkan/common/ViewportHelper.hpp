#pragma once

#include <vulkan/vulkan.hpp>

namespace Kataglyphis {

// Every raster pass in this engine renders to the full extent of its target
// with an unflipped y axis and the standard [0, 1] depth range - there is no
// pass that needs a partial viewport, a flipped y, or a non-default depth
// range. A pass that DOES need one of those must build the vk::Viewport /
// vk::Rect2D inline and say why, rather than growing this helper new
// parameters - that is what would eventually let a flipped viewport or a
// depth slice silently sneak into every other call site.
constexpr vk::Viewport fullExtentViewport(vk::Extent2D extent)
{
    return vk::Viewport{ 0.0F,
        0.0F,
        static_cast<float>(extent.width),
        static_cast<float>(extent.height),
        0.0F,
        1.0F };
}

constexpr vk::Rect2D fullExtentScissor(vk::Extent2D extent) { return vk::Rect2D{ vk::Offset2D{ 0, 0 }, extent }; }

inline void setFullExtentViewportAndScissor(vk::CommandBuffer commandBuffer, vk::Extent2D extent)
{
    const vk::Viewport viewport = fullExtentViewport(extent);
    commandBuffer.setViewport(0, viewport);
    const vk::Rect2D scissor = fullExtentScissor(extent);
    commandBuffer.setScissor(0, scissor);
}

}// namespace Kataglyphis
