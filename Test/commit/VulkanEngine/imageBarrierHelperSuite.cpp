// Direct unit coverage for common/ImageBarrierHelper.hpp's
// buildImageMemoryBarrier - the helper that replaced seven hand-written
// vk::ImageMemoryBarrier literals across Raytracing.cpp, PathTracing.cpp and
// FrameCapture.ixx.
//
// vk::ImageMemoryBarrier is a plain struct, so every one of those call sites
// can be re-created here and pinned field-by-field with no device. This
// guards the class of defect no pixel oracle sees: a dropped queue-family
// index or a wrong subresource-range field still renders correctly (the
// validation layers may not even be running), and only corrupts
// synchronisation under specific timing.

#include <gtest/gtest.h>

#include <vulkan/vulkan.hpp>

#include "common/ImageBarrierHelper.hpp"

using Kataglyphis::buildImageMemoryBarrier;

// Usable in a constant expression, matching RenderPassHelper.hpp's convention.
// vk::Image's only constexpr constructor takes std::nullptr_t (its default
// constructor is deliberately NOT constexpr - see vulkan_handles.hpp's "try
// to workaround a compiler issue" comment), so nullptr stands in for a null
// image handle here rather than vk::Image{}.
static_assert(buildImageMemoryBarrier(vk::Image(nullptr), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, {},
                 vk::AccessFlagBits::eShaderWrite)
                  .newLayout
                == vk::ImageLayout::eGeneral,
  "buildImageMemoryBarrier must be usable in a constant expression");

namespace {

// Fields every call site in this engine agrees on. Asserted once per test
// rather than once overall, so no individual site can drift away from them.
void expectEngineWideBarrierInvariants(const vk::ImageMemoryBarrier &barrier)
{
    EXPECT_EQ(barrier.srcQueueFamilyIndex, vk::QueueFamilyIgnored);
    EXPECT_EQ(barrier.dstQueueFamilyIndex, vk::QueueFamilyIgnored);
    EXPECT_EQ(barrier.subresourceRange.aspectMask, vk::ImageAspectFlagBits::eColor);
    EXPECT_EQ(barrier.subresourceRange.baseMipLevel, 0U);
    EXPECT_EQ(barrier.subresourceRange.levelCount, 1U);
    EXPECT_EQ(barrier.subresourceRange.baseArrayLayer, 0U);
    EXPECT_EQ(barrier.subresourceRange.layerCount, 1U);
}

TEST(ImageBarrierHelperUnit, DefaultsToFullColorMipAndLayerRange)
{
    const vk::ImageMemoryBarrier barrier = buildImageMemoryBarrier(
      vk::Image{}, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, {}, vk::AccessFlagBits::eShaderWrite);

    expectEngineWideBarrierInvariants(barrier);
}

TEST(ImageBarrierHelperUnit, MatchesRaytracingRasterizerToRaytracingBarrier)
{
    // Raytracing::recordCommands, rasterizerToRaytracingImageBarrier.
    const vk::ImageMemoryBarrier barrier = buildImageMemoryBarrier(
      vk::Image{}, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, {}, vk::AccessFlagBits::eShaderWrite);

    EXPECT_EQ(barrier.srcAccessMask, vk::AccessFlags{});
    EXPECT_EQ(barrier.dstAccessMask, vk::AccessFlagBits::eShaderWrite);
    EXPECT_EQ(barrier.oldLayout, vk::ImageLayout::eUndefined);
    EXPECT_EQ(barrier.newLayout, vk::ImageLayout::eGeneral);
    expectEngineWideBarrierInvariants(barrier);
}

TEST(ImageBarrierHelperUnit, MatchesRaytracingRaytracingToPostBarrier)
{
    // Raytracing::recordCommands, raytracingToPostImageBarrier.
    const vk::ImageMemoryBarrier barrier = buildImageMemoryBarrier(vk::Image{},
      vk::ImageLayout::eGeneral,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::AccessFlagBits::eShaderWrite,
      vk::AccessFlagBits::eShaderRead);

    EXPECT_EQ(barrier.srcAccessMask, vk::AccessFlagBits::eShaderWrite);
    EXPECT_EQ(barrier.dstAccessMask, vk::AccessFlagBits::eShaderRead);
    EXPECT_EQ(barrier.oldLayout, vk::ImageLayout::eGeneral);
    EXPECT_EQ(barrier.newLayout, vk::ImageLayout::eShaderReadOnlyOptimal);
    expectEngineWideBarrierInvariants(barrier);
}

TEST(ImageBarrierHelperUnit, MatchesPathTracingPresentToPathTracingBarrier)
{
    // PathTracing::recordCommands, presentToPathTracingImageBarrier.
    const vk::ImageMemoryBarrier barrier = buildImageMemoryBarrier(
      vk::Image{}, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, {}, vk::AccessFlagBits::eShaderWrite);

    EXPECT_EQ(barrier.srcAccessMask, vk::AccessFlags{});
    EXPECT_EQ(barrier.dstAccessMask, vk::AccessFlagBits::eShaderWrite);
    EXPECT_EQ(barrier.oldLayout, vk::ImageLayout::eUndefined);
    EXPECT_EQ(barrier.newLayout, vk::ImageLayout::eGeneral);
    expectEngineWideBarrierInvariants(barrier);
}

TEST(ImageBarrierHelperUnit, MatchesPathTracingAccumulationBarrier)
{
    // PathTracing::recordCommands, accumulationBarrier - the sole call site
    // whose oldLayout and newLayout are the same (a read-modify-write hazard,
    // not a layout transition) and whose dstAccessMask ORs together two bits.
    const vk::ImageMemoryBarrier barrier = buildImageMemoryBarrier(vk::Image{},
      vk::ImageLayout::eGeneral,
      vk::ImageLayout::eGeneral,
      vk::AccessFlagBits::eShaderWrite,
      vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);

    EXPECT_EQ(barrier.srcAccessMask, vk::AccessFlagBits::eShaderWrite);
    EXPECT_EQ(barrier.dstAccessMask, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
    EXPECT_EQ(barrier.oldLayout, barrier.newLayout);
    expectEngineWideBarrierInvariants(barrier);
}

TEST(ImageBarrierHelperUnit, MatchesPathTracingToPresentBarrier)
{
    // PathTracing::recordCommands, pathTracingToPresentImageBarrier.
    const vk::ImageMemoryBarrier barrier = buildImageMemoryBarrier(vk::Image{},
      vk::ImageLayout::eGeneral,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::AccessFlagBits::eShaderWrite,
      vk::AccessFlagBits::eShaderRead);

    EXPECT_EQ(barrier.srcAccessMask, vk::AccessFlagBits::eShaderWrite);
    EXPECT_EQ(barrier.dstAccessMask, vk::AccessFlagBits::eShaderRead);
    EXPECT_EQ(barrier.oldLayout, vk::ImageLayout::eGeneral);
    EXPECT_EQ(barrier.newLayout, vk::ImageLayout::eShaderReadOnlyOptimal);
    expectEngineWideBarrierInvariants(barrier);
}

TEST(ImageBarrierHelperUnit, MatchesFrameCaptureToTransferSrcBarrier)
{
    // FrameCapture::record, to_transfer_src.
    const vk::ImageMemoryBarrier barrier = buildImageMemoryBarrier(vk::Image{},
      vk::ImageLayout::ePresentSrcKHR,
      vk::ImageLayout::eTransferSrcOptimal,
      vk::AccessFlagBits::eColorAttachmentWrite,
      vk::AccessFlagBits::eTransferRead);

    EXPECT_EQ(barrier.srcAccessMask, vk::AccessFlagBits::eColorAttachmentWrite);
    EXPECT_EQ(barrier.dstAccessMask, vk::AccessFlagBits::eTransferRead);
    EXPECT_EQ(barrier.oldLayout, vk::ImageLayout::ePresentSrcKHR);
    EXPECT_EQ(barrier.newLayout, vk::ImageLayout::eTransferSrcOptimal);
    expectEngineWideBarrierInvariants(barrier);
}

TEST(ImageBarrierHelperUnit, MatchesFrameCaptureBackToPresentBarrier)
{
    // FrameCapture::record, back_to_present - the sole call site whose
    // dstAccessMask is deliberately empty (nothing reads through this
    // barrier; the buffer-to-host visibility is a separate
    // vk::BufferMemoryBarrier in the same pipelineBarrier call).
    const vk::ImageMemoryBarrier barrier = buildImageMemoryBarrier(vk::Image{},
      vk::ImageLayout::eTransferSrcOptimal,
      vk::ImageLayout::ePresentSrcKHR,
      vk::AccessFlagBits::eTransferRead,
      vk::AccessFlags{});

    EXPECT_EQ(barrier.srcAccessMask, vk::AccessFlagBits::eTransferRead);
    EXPECT_EQ(barrier.dstAccessMask, vk::AccessFlags{});
    EXPECT_EQ(barrier.oldLayout, vk::ImageLayout::eTransferSrcOptimal);
    EXPECT_EQ(barrier.newLayout, vk::ImageLayout::ePresentSrcKHR);
    expectEngineWideBarrierInvariants(barrier);
}

}// namespace
