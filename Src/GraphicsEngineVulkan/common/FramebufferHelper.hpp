#pragma once

#include <span>
#include <vector>

#include <vulkan/vulkan.hpp>

namespace Kataglyphis {

// Every framebuffer in this engine is built from a render pass, a flat list
// of attachment views, and the target extent - Rasterizer, PostStage,
// DeferredRasterizer, SkyBox and CascadedShadowMap each spelled out the same
// five field assignments, and one of the five (SkyBox) had its
// attachmentCount hard-coded to 2 rather than derived from the array it was
// handed.
//
// attachmentCount is deliberately DERIVED from attachments.size() rather than
// taken as a parameter, so the count can never drift from the span actually
// passed in.
//
// Lifetime note: the returned vk::FramebufferCreateInfo borrows
// attachments.data() - it must be consumed by createFramebuffer before the
// span's backing storage goes out of scope or is mutated for a different
// framebuffer.
//
// A pass that needs an imageless framebuffer or another flag must build the
// vk::FramebufferCreateInfo inline and say why, rather than growing this
// helper a parameter - the same rule RenderPassHelper.hpp and
// ViewportHelper.hpp state, for the same reason.
//
// Built via the fully-explicit vk::FramebufferCreateInfo constructor rather
// than value-init-then-assign: value-init (`vk::FramebufferCreateInfo{}`)
// defaults every unspecified parameter, including `RenderPass renderPass_ =
// {}`, and that default expands to RenderPass's non-constexpr default
// constructor - which would make this function impossible to call in a
// constant expression no matter what the caller passes.
constexpr vk::FramebufferCreateInfo buildFramebufferCreateInfo(vk::RenderPass render_pass,
  std::span<const vk::ImageView> attachments,
  vk::Extent2D extent,
  uint32_t layers = 1)
{
    return vk::FramebufferCreateInfo{ vk::FramebufferCreateFlags{}, render_pass,
        static_cast<uint32_t>(attachments.size()), attachments.data(), extent.width, extent.height, layers };
}

// Destroys every framebuffer in the vector and clears it, and destroys a
// single framebuffer and nulls its handle - the same idempotence rule
// PipelineLayoutHelper.hpp's destroyPipelineAndLayout follows: a device-less
// call (already torn down, or never had a device) is a no-op rather than a
// crash, so an explicit cleanUp followed by the destructor's safety net stays
// safe. The target is taken by reference for the same reason
// destroyPipelineAndLayout takes its handles by reference; passing by value
// would destroy without clearing/nulling and leave the caller holding a
// dangling handle.
inline void destroyFramebuffers(vk::Device device, std::vector<vk::Framebuffer> &framebuffers)
{
    if (!device) { return; }
    for (auto &framebuffer : framebuffers) {
        if (framebuffer) { device.destroyFramebuffer(framebuffer); }
    }
    framebuffers.clear();
}

inline void destroyFramebuffer(vk::Device device, vk::Framebuffer &framebuffer)
{
    if (!device) { return; }
    if (framebuffer) {
        device.destroyFramebuffer(framebuffer);
        framebuffer = nullptr;
    }
}

}// namespace Kataglyphis
