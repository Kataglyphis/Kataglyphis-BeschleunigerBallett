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

}// namespace Kataglyphis
