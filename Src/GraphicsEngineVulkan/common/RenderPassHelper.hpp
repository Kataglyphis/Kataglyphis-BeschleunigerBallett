#pragma once

#include <span>

#include <vulkan/vulkan.hpp>

namespace Kataglyphis {

// Every render pass in this engine declared its attachments the same way, by
// hand, in five files: Rasterizer, DeferredRasterizer, PostStage, SkyBox and
// CascadedShadowMap each spelled out the same eight or nine field
// assignments. Three of those fields are the same in EVERY pass and are the
// ones a copy-paste silently drops:
//
//   samples         = e1        - nothing here is multisampled
//   stencilLoadOp   = eDontCare - no pass reads or writes a stencil aspect,
//   stencilStoreOp  = eDontCare   even when the chosen depth format has one
//                                 (chooseDepthFormat may return eD32SfloatS8Uint)
//
// so they are baked in here rather than repeated. The five fields the passes
// genuinely disagree on stay parameters, with the majority variant as the
// default: clear on load, store the result, and start from eUndefined (i.e.
// discard whatever the previous frame left). PostStage is the one pass that
// overrides all three - it LOADS an already-rendered colour attachment and
// hands it to the presentation engine.
//
// A pass that needs a multisampled or stencil-carrying attachment must build
// the vk::AttachmentDescription inline and say why, rather than growing this
// helper new parameters - the same rule ViewportHelper.hpp states, for the
// same reason.
constexpr vk::AttachmentDescription buildAttachmentDescription(vk::Format format,
  vk::ImageLayout final_layout,
  vk::AttachmentLoadOp load_op = vk::AttachmentLoadOp::eClear,
  vk::AttachmentStoreOp store_op = vk::AttachmentStoreOp::eStore,
  vk::ImageLayout initial_layout = vk::ImageLayout::eUndefined)
{
    vk::AttachmentDescription description{};
    description.format = format;
    description.samples = vk::SampleCountFlagBits::e1;
    description.loadOp = load_op;
    description.storeOp = store_op;
    description.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    description.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    description.initialLayout = initial_layout;
    description.finalLayout = final_layout;
    return description;
}

// Every render pass begin in this engine spelled out the same five field
// assignments by hand, in Rasterizer, PostStage, DeferredRasterizer, SkyBox
// and CascadedShadowMap - two of the five hard-coded clearValueCount as a
// literal rather than deriving it from the clear-value array actually
// passed.
//
// renderArea.offset is always {0, 0} and the extent is always the target's
// full extent - every call site follows the beginRenderPass with
// setFullExtentViewportAndScissor on that same extent. clearValueCount is
// deliberately DERIVED from clear_values.size() rather than taken as a
// parameter, so it can never drift from the array actually passed.
//
// A pass that needs a partial render area must build the
// vk::RenderPassBeginInfo inline and say why, rather than growing this
// helper new parameters - the same rule FramebufferHelper.hpp and
// ViewportHelper.hpp state, for the same reason.
//
// Built via the fully-explicit vk::RenderPassBeginInfo constructor rather
// than value-init-then-assign, for the same constexpr reason
// FramebufferHelper.hpp documents.
constexpr vk::RenderPassBeginInfo buildRenderPassBeginInfo(vk::RenderPass render_pass,
  vk::Framebuffer framebuffer,
  vk::Extent2D extent,
  std::span<const vk::ClearValue> clear_values)
{
    return vk::RenderPassBeginInfo{ render_pass, framebuffer, vk::Rect2D{ vk::Offset2D{ 0, 0 }, extent },
        static_cast<uint32_t>(clear_values.size()), clear_values.data() };
}

// Every render pass in this engine spelled out the same six field
// assignments by hand, in Rasterizer, PostStage, DeferredRasterizer, SkyBox
// and CascadedShadowMap - SkyBox hard-coded attachmentCount = 2 next to a
// two-element std::array instead of deriving it, exactly as it did for
// FramebufferHelper.hpp's attachmentCount.
//
// attachmentCount, subpassCount and dependencyCount are all deliberately
// DERIVED from their span's .size() rather than taken as parameters, so none
// of the three can drift from the array actually passed in.
//
// Lifetime note: the returned vk::RenderPassCreateInfo borrows all three
// spans' .data() pointers - they must outlive the createRenderPass call that
// consumes it.
//
// flags and pNext are deliberately left at their defaults so a pass that
// needs either - CascadedShadowMap chains a
// vk::RenderPassMultiviewCreateInfo through pNext - assigns it on the
// returned value rather than this helper growing a parameter, the same rule
// RenderPassHelper.hpp's other two helpers and FramebufferHelper.hpp state.
//
// Built via the fully-explicit vk::RenderPassCreateInfo constructor rather
// than value-init-then-assign, for the same constexpr reason
// FramebufferHelper.hpp documents.
// Every render pass in this engine spelled out the same three or four field
// assignments by hand, in Rasterizer, PostStage, DeferredRasterizer, SkyBox
// and CascadedShadowMap - three of the five hard-coded colorAttachmentCount
// as a literal (usually 1) next to a single-element reference instead of
// deriving it, the same drift buildRenderPassCreateInfo's attachmentCount
// fixed for the attachment array.
//
// colorAttachmentCount and inputAttachmentCount are both deliberately
// DERIVED from their span's .size() rather than taken as parameters.
// pipelineBindPoint is always eGraphics: a compute or ray-tracing subpass
// does not go through a vk::RenderPass at all, so none of the five passes
// this replaced ever set anything else.
//
// pResolveAttachments, preserveAttachmentCount and flags are deliberately
// left at their defaults - nothing in this engine resolves a multisampled
// attachment or preserves one across subpasses. A pass that needs either
// must build the vk::SubpassDescription inline and say why, rather than
// growing this helper new parameters - the same rule this file's other two
// helpers and ViewportHelper.hpp state.
//
// Lifetime note: the returned vk::SubpassDescription borrows both spans'
// .data() pointers and depth_attachment - they must all outlive the
// createRenderPass call that consumes it (via buildRenderPassCreateInfo).
constexpr vk::SubpassDescription buildSubpassDescription(
  std::span<const vk::AttachmentReference> color_attachments,
  const vk::AttachmentReference *depth_attachment,
  std::span<const vk::AttachmentReference> input_attachments = {})
{
    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = static_cast<uint32_t>(color_attachments.size());
    subpass.pColorAttachments = color_attachments.data();
    subpass.pDepthStencilAttachment = depth_attachment;
    subpass.inputAttachmentCount = static_cast<uint32_t>(input_attachments.size());
    subpass.pInputAttachments = input_attachments.data();
    return subpass;
}

constexpr vk::RenderPassCreateInfo buildRenderPassCreateInfo(
  std::span<const vk::AttachmentDescription> attachments,
  std::span<const vk::SubpassDescription> subpasses,
  std::span<const vk::SubpassDependency> dependencies)
{
    return vk::RenderPassCreateInfo{ vk::RenderPassCreateFlags{}, static_cast<uint32_t>(attachments.size()),
        attachments.data(), static_cast<uint32_t>(subpasses.size()), subpasses.data(),
        static_cast<uint32_t>(dependencies.size()), dependencies.data() };
}

// Destroys a render pass and nulls its handle - the same idempotence rule
// FramebufferHelper.hpp's destroyFramebuffer/destroyFramebuffers and
// PipelineLayoutHelper.hpp's destroyPipelineAndLayout follow: a device-less
// call (already torn down, or never had a device) is a no-op rather than a
// crash, so an explicit cleanUp followed by the destructor's safety net stays
// safe. Rasterizer, DeferredRasterizer, PostStage, SkyBox and
// CascadedShadowMap each hand-rolled this. The handle is taken by reference
// for the same reason destroyPipelineAndLayout takes its handles by
// reference; passing by value would destroy without nulling and leave the
// caller holding a dangling handle.
inline void destroyRenderPass(vk::Device device, vk::RenderPass &render_pass)
{
    if (!device) { return; }
    if (render_pass) {
        device.destroyRenderPass(render_pass);
        render_pass = nullptr;
    }
}

}// namespace Kataglyphis
