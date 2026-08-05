#pragma once

#include <vulkan/vulkan.hpp>

namespace Kataglyphis {

// Every image memory barrier in this engine spelled out the same eight or
// nine field assignments by hand, across seven call sites in three files:
// Raytracing.cpp (rasterizerToRaytracingImageBarrier,
// raytracingToPostImageBarrier), PathTracing.cpp
// (presentToPathTracingImageBarrier, accumulationBarrier,
// pathTracingToPresentImageBarrier) and FrameCapture.ixx (to_transfer_src,
// back_to_present) - already drifted into two spellings of "ignored queue
// family" (VK_QUEUE_FAMILY_IGNORED in Raytracing.cpp/PathTracing.cpp,
// vk::QueueFamilyIgnored in FrameCapture.ixx - both name the same value;
// this helper standardises on the latter) and two constructions of the same
// {eColor, 0, 1, 0, 1} subresource range.
//
// aspect/baseMipLevel/levelCount/baseArrayLayer/layerCount default to that
// single full-colour, one-mip, one-layer range - every one of the seven call
// sites this replaced used exactly it. A barrier that needs a real subrange
// (a mip chain, a stencil aspect) must build the vk::ImageMemoryBarrier
// inline and say why, rather than growing this helper's defaults - the same
// rule RenderPassHelper.hpp, FramebufferHelper.hpp and
// PipelineLayoutHelper.hpp state. VulkanImage.cpp and Texture.cpp already do
// exactly that: their aspect/mip/layer values come from a general
// transition helper's own parameters, not boilerplate, so they are exempt
// and stay unconverted. The two cloud-output barriers in
// VulkanRenderer.cpp - cloud_output_war_barrier and cloud_output_barrier -
// are also deliberately left alone; they are the subject of a separate,
// blocked backlog entry.
//
// pNext is deliberately left at its default; nothing in this engine chains
// anything through an image barrier.
constexpr vk::ImageMemoryBarrier buildImageMemoryBarrier(vk::Image image,
  vk::ImageLayout oldLayout,
  vk::ImageLayout newLayout,
  vk::AccessFlags srcAccess,
  vk::AccessFlags dstAccess,
  vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor,
  uint32_t baseMipLevel = 0,
  uint32_t levelCount = 1,
  uint32_t baseArrayLayer = 0,
  uint32_t layerCount = 1)
{
    return vk::ImageMemoryBarrier{ srcAccess, dstAccess, oldLayout, newLayout, vk::QueueFamilyIgnored,
        vk::QueueFamilyIgnored, image,
        vk::ImageSubresourceRange{ aspect, baseMipLevel, levelCount, baseArrayLayer, layerCount } };
}

}// namespace Kataglyphis
