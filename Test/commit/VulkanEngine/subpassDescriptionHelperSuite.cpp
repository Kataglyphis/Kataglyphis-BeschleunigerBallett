// Direct unit coverage for common/RenderPassHelper.hpp's
// buildSubpassDescription - the helper that replaced five hand-written
// vk::SubpassDescription blocks across Rasterizer, PostStage,
// DeferredRasterizer, SkyBox and CascadedShadowMap.
//
// Three of those five hard-coded colorAttachmentCount = 1 as a literal next
// to a single-element reference instead of deriving it from the span
// actually passed - the same drift buildRenderPassCreateInfo's
// attachmentCount and buildFramebufferCreateInfo's attachmentCount already
// guard against for their respective arrays.

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <vulkan/vulkan.hpp>

#include "common/RenderPassHelper.hpp"

using Kataglyphis::buildSubpassDescription;

namespace {
constexpr vk::AttachmentReference kColorRef{ 0, vk::ImageLayout::eColorAttachmentOptimal };
constexpr vk::AttachmentReference kDepthRef{ 1, vk::ImageLayout::eDepthStencilAttachmentOptimal };
constexpr std::array<vk::AttachmentReference, 3> kThreeColorRefs{
    vk::AttachmentReference{ 1, vk::ImageLayout::eColorAttachmentOptimal },
    vk::AttachmentReference{ 2, vk::ImageLayout::eColorAttachmentOptimal },
    vk::AttachmentReference{ 3, vk::ImageLayout::eColorAttachmentOptimal }
};
constexpr std::array<vk::AttachmentReference, 4> kFourInputRefs{
    vk::AttachmentReference{ 1, vk::ImageLayout::eShaderReadOnlyOptimal },
    vk::AttachmentReference{ 2, vk::ImageLayout::eShaderReadOnlyOptimal },
    vk::AttachmentReference{ 3, vk::ImageLayout::eShaderReadOnlyOptimal },
    vk::AttachmentReference{ 4, vk::ImageLayout::eShaderReadOnlyOptimal }
};
}// namespace

// Usable in a constant expression, matching the other RenderPassHelper.hpp
// helpers' convention.
static_assert(buildSubpassDescription(std::span<const vk::AttachmentReference>(&kColorRef, 1), &kDepthRef)
                .colorAttachmentCount
    == 1U,
  "buildSubpassDescription must be usable in a constant expression");

namespace {

TEST(SubpassDescriptionHelperUnit, ColorAttachmentCountIsDerivedFromASingleElementSpan)
{
    // The Rasterizer / PostStage / SkyBox shape: one colour ref, one depth ref.
    const vk::SubpassDescription subpass =
      buildSubpassDescription(std::span<const vk::AttachmentReference>(&kColorRef, 1), &kDepthRef);

    EXPECT_EQ(subpass.colorAttachmentCount, 1U);
    EXPECT_EQ(subpass.pColorAttachments, &kColorRef);
}

TEST(SubpassDescriptionHelperUnit, ColorAttachmentCountIsDerivedFromAThreeElementSpan)
{
    // The DeferredRasterizer geometry-subpass shape: normal, albedo, material.
    const vk::SubpassDescription subpass =
      buildSubpassDescription(std::span<const vk::AttachmentReference>(kThreeColorRefs), &kDepthRef);

    EXPECT_EQ(subpass.colorAttachmentCount, 3U);
    EXPECT_EQ(subpass.pColorAttachments, kThreeColorRefs.data());
}

TEST(SubpassDescriptionHelperUnit, ColorAttachmentCountIsZeroForAnEmptySpan)
{
    // The CascadedShadowMap shape: depth-only, no colour attachment at all.
    const vk::SubpassDescription subpass =
      buildSubpassDescription(std::span<const vk::AttachmentReference>{}, &kDepthRef);

    EXPECT_EQ(subpass.colorAttachmentCount, 0U);
}

TEST(SubpassDescriptionHelperUnit, DepthStencilAttachmentPointerRoundTrips)
{
    const vk::SubpassDescription subpass =
      buildSubpassDescription(std::span<const vk::AttachmentReference>(&kColorRef, 1), &kDepthRef);

    EXPECT_EQ(subpass.pDepthStencilAttachment, &kDepthRef);
}

TEST(SubpassDescriptionHelperUnit, DepthStencilAttachmentIsNullWhenNoneIsPassed)
{
    // The DeferredRasterizer lighting-subpass shape: colour + input
    // attachments, no depth attachment of its own.
    const vk::SubpassDescription subpass = buildSubpassDescription(
      std::span<const vk::AttachmentReference>(&kColorRef, 1), nullptr,
      std::span<const vk::AttachmentReference>(kFourInputRefs));

    EXPECT_EQ(subpass.pDepthStencilAttachment, nullptr);
}

TEST(SubpassDescriptionHelperUnit, InputAttachmentCountIsZeroWhenTheDefaultedArgumentIsOmitted)
{
    const vk::SubpassDescription subpass =
      buildSubpassDescription(std::span<const vk::AttachmentReference>(&kColorRef, 1), &kDepthRef);

    EXPECT_EQ(subpass.inputAttachmentCount, 0U);
    EXPECT_EQ(subpass.pInputAttachments, nullptr);
}

TEST(SubpassDescriptionHelperUnit, InputAttachmentCountIsFourForTheDeferredLightingShape)
{
    const vk::SubpassDescription subpass = buildSubpassDescription(
      std::span<const vk::AttachmentReference>(&kColorRef, 1), nullptr,
      std::span<const vk::AttachmentReference>(kFourInputRefs));

    EXPECT_EQ(subpass.inputAttachmentCount, 4U);
    EXPECT_EQ(subpass.pInputAttachments, kFourInputRefs.data());
}

TEST(SubpassDescriptionHelperUnit, PipelineBindPointIsAlwaysGraphics)
{
    const vk::SubpassDescription subpass =
      buildSubpassDescription(std::span<const vk::AttachmentReference>(&kColorRef, 1), &kDepthRef);

    EXPECT_EQ(subpass.pipelineBindPoint, vk::PipelineBindPoint::eGraphics);
}

TEST(SubpassDescriptionHelperUnit, ResolveAttachmentsAreLeftNull)
{
    const vk::SubpassDescription subpass =
      buildSubpassDescription(std::span<const vk::AttachmentReference>(&kColorRef, 1), &kDepthRef);

    EXPECT_EQ(subpass.pResolveAttachments, nullptr);
}

// One test per call site this helper replaced, each pinning the exact
// vk::SubpassDescription that call site's createRenderPass now produces -
// the way renderPassHelperSuite.cpp pins buildAttachmentDescription's callers.

TEST(SubpassDescriptionHelperUnit, MatchesRasterizerSubpass)
{
    // Rasterizer::createRenderPass: colour at 0, depth at 1, no input attachments.
    constexpr vk::AttachmentReference color_ref{ 0, vk::ImageLayout::eColorAttachmentOptimal };
    constexpr vk::AttachmentReference depth_ref{ 1, vk::ImageLayout::eDepthStencilAttachmentOptimal };

    const vk::SubpassDescription subpass =
      buildSubpassDescription(std::span<const vk::AttachmentReference>(&color_ref, 1), &depth_ref);

    EXPECT_EQ(subpass.colorAttachmentCount, 1U);
    EXPECT_EQ(subpass.pDepthStencilAttachment, &depth_ref);
    EXPECT_EQ(subpass.inputAttachmentCount, 0U);
    EXPECT_EQ(subpass.pipelineBindPoint, vk::PipelineBindPoint::eGraphics);
}

TEST(SubpassDescriptionHelperUnit, MatchesPostStageSubpass)
{
    // PostStage::createRenderPass: same colour-at-0/depth-at-1 shape as
    // Rasterizer, but over the swapchain colour target and a reused depth
    // buffer rather than an offscreen one.
    constexpr vk::AttachmentReference color_ref{ 0, vk::ImageLayout::eColorAttachmentOptimal };
    constexpr vk::AttachmentReference depth_ref{ 1, vk::ImageLayout::eDepthStencilAttachmentOptimal };

    const vk::SubpassDescription subpass =
      buildSubpassDescription(std::span<const vk::AttachmentReference>(&color_ref, 1), &depth_ref);

    EXPECT_EQ(subpass.colorAttachmentCount, 1U);
    EXPECT_EQ(subpass.pDepthStencilAttachment, &depth_ref);
    EXPECT_EQ(subpass.inputAttachmentCount, 0U);
}

TEST(SubpassDescriptionHelperUnit, MatchesSkyBoxSubpass)
{
    // SkyBox::createRenderPass: colour at 0, depth at 1, no input attachments.
    constexpr vk::AttachmentReference color_ref{ 0, vk::ImageLayout::eColorAttachmentOptimal };
    constexpr vk::AttachmentReference depth_ref{ 1, vk::ImageLayout::eDepthStencilAttachmentOptimal };

    const vk::SubpassDescription subpass =
      buildSubpassDescription(std::span<const vk::AttachmentReference>(&color_ref, 1), &depth_ref);

    EXPECT_EQ(subpass.colorAttachmentCount, 1U);
    EXPECT_EQ(subpass.pDepthStencilAttachment, &depth_ref);
    EXPECT_EQ(subpass.inputAttachmentCount, 0U);
}

TEST(SubpassDescriptionHelperUnit, MatchesCascadedShadowMapSubpass)
{
    // CascadedShadowMap::createRenderPass: depth-only, no colour attachment
    // at all - the sole call site with an empty colour span.
    constexpr vk::AttachmentReference depth_ref{ 0, vk::ImageLayout::eDepthStencilAttachmentOptimal };

    const vk::SubpassDescription subpass =
      buildSubpassDescription(std::span<const vk::AttachmentReference>{}, &depth_ref);

    EXPECT_EQ(subpass.colorAttachmentCount, 0U);
    EXPECT_EQ(subpass.pDepthStencilAttachment, &depth_ref);
    EXPECT_EQ(subpass.inputAttachmentCount, 0U);
}

TEST(SubpassDescriptionHelperUnit, MatchesDeferredRasterizerGeometrySubpass)
{
    // DeferredRasterizer::createRenderPass subpass 0: three G-buffer colour
    // attachments (normal, albedo, material) plus depth - the only
    // three-element colour span among the five call sites.
    const vk::SubpassDescription subpass =
      buildSubpassDescription(std::span<const vk::AttachmentReference>(kThreeColorRefs), &kDepthRef);

    EXPECT_EQ(subpass.colorAttachmentCount, 3U);
    EXPECT_EQ(subpass.pColorAttachments, kThreeColorRefs.data());
    EXPECT_EQ(subpass.pDepthStencilAttachment, &kDepthRef);
    EXPECT_EQ(subpass.inputAttachmentCount, 0U);
}

TEST(SubpassDescriptionHelperUnit, MatchesDeferredRasterizerLightingSubpass)
{
    // DeferredRasterizer::createRenderPass subpass 1: writes the final colour
    // attachment while reading the geometry subpass's three G-buffers plus
    // depth back as input attachments - no depth attachment of its own.
    const vk::SubpassDescription subpass = buildSubpassDescription(
      std::span<const vk::AttachmentReference>(&kColorRef, 1), nullptr,
      std::span<const vk::AttachmentReference>(kFourInputRefs));

    EXPECT_EQ(subpass.colorAttachmentCount, 1U);
    EXPECT_EQ(subpass.pDepthStencilAttachment, nullptr);
    EXPECT_EQ(subpass.inputAttachmentCount, 4U);
    EXPECT_EQ(subpass.pInputAttachments, kFourInputRefs.data());
}

}// namespace
