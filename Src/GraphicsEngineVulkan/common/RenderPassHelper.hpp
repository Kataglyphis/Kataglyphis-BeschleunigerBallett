#pragma once

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

}// namespace Kataglyphis
