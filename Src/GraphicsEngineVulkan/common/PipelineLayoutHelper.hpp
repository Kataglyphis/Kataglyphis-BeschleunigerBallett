#pragma once

#include <span>

#include <vulkan/vulkan.hpp>

namespace Kataglyphis {

// Every pipeline layout in this engine spelled out the same field
// assignments by hand, in nine places across eight files: Clouds,
// Raytracing, SkyBox, Rasterizer, PathTracing, PostStage,
// CascadedShadowMap and DeferredRasterizer (geometry and lighting).
// SkyBox hard-coded setLayoutCount = 2 next to a two-element std::array
// instead of deriving it, exactly as it did for FramebufferHelper.hpp's
// attachmentCount and RenderPassHelper.hpp's attachmentCount.
//
// setLayoutCount and pushConstantRangeCount are both deliberately DERIVED
// from their span's .size() rather than taken as parameters, so neither can
// drift from the array actually passed in. The defaulted empty
// push_constant_ranges span is what lets Clouds and the deferred lighting
// pass drop their "no push constants" special case entirely.
//
// Lifetime note: the returned vk::PipelineLayoutCreateInfo borrows both
// spans' .data() pointers - they must outlive the createPipelineLayout call
// that consumes it.
//
// flags is deliberately left at its default so a pass that needs one -
// none does today - assigns it on the returned value rather than this
// helper growing a parameter, the same rule RenderPassHelper.hpp's and
// FramebufferHelper.hpp's helpers state.
//
// Built via the fully-explicit vk::PipelineLayoutCreateInfo constructor
// rather than value-init-then-assign, for the same constexpr reason
// FramebufferHelper.hpp documents.
constexpr vk::PipelineLayoutCreateInfo buildPipelineLayoutCreateInfo(
  std::span<const vk::DescriptorSetLayout> set_layouts,
  std::span<const vk::PushConstantRange> push_constant_ranges = {})
{
    return vk::PipelineLayoutCreateInfo{ vk::PipelineLayoutCreateFlags{}, static_cast<uint32_t>(set_layouts.size()),
        set_layouts.data(), static_cast<uint32_t>(push_constant_ranges.size()), push_constant_ranges.data() };
}

// Destroys a pipeline and the layout it was built with, and nulls both
// handles so a second call (an explicit cleanUp followed by the
// destructor) is a no-op - the same idempotence rule VulkanBuffer and
// VulkanImage follow. Handles are taken by reference for exactly that
// reason; passing by value would destroy without nulling and the caller
// would keep a dangling handle.
inline void destroyPipelineAndLayout(vk::Device device, vk::Pipeline &pipeline, vk::PipelineLayout &layout)
{
    if (!device) { return; }
    if (pipeline) {
        device.destroyPipeline(pipeline);
        pipeline = nullptr;
    }
    if (layout) {
        device.destroyPipelineLayout(layout);
        layout = nullptr;
    }
}

}// namespace Kataglyphis
