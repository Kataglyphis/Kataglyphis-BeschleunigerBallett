// Direct unit coverage for common/RenderPassHelper.hpp's
// buildAttachmentDescription - the helper that replaced eight hand-written
// vk::AttachmentDescription literals across Rasterizer, DeferredRasterizer,
// PostStage, SkyBox and CascadedShadowMap.
//
// vk::AttachmentDescription is a plain struct, so every one of those call
// sites can be re-created here and pinned field-by-field with no device. Two
// things this guards:
//
//   1. The three constants the helper bakes in (samples e1, both stencil ops
//      eDontCare) really do reach every attachment. A copy-paste that drops
//      stencilStoreOp is exactly the kind of defect no pixel oracle sees -
//      chooseDepthFormat may return eD32SfloatS8Uint, and a stencil aspect
//      left at eStore silently keeps a writeback alive.
//   2. The five per-pass fields still carry each site's ORIGINAL values, so
//      the extraction cannot have quietly changed a load op or an initial
//      layout. PostStage is the sole site that overrides all three defaults;
//      Rasterizer's depth is the sole site with eDontCare on a depth store
//      out of an eUndefined initial layout.

#include <gtest/gtest.h>

#include <array>
#include <vulkan/vulkan.hpp>

#include "common/RenderPassHelper.hpp"

using Kataglyphis::buildAttachmentDescription;

// Usable in a constant expression, matching ViewportHelper.hpp's convention.
static_assert(
  buildAttachmentDescription(vk::Format::eR16G16B16A16Sfloat, vk::ImageLayout::eShaderReadOnlyOptimal).samples
    == vk::SampleCountFlagBits::e1,
  "buildAttachmentDescription must be usable in a constant expression");

namespace {

// Fields identical in EVERY render pass of the engine. Asserted once per call
// site below rather than once overall: the point is that no individual site
// can drift away from them.
void expectEngineWideAttachmentInvariants(const vk::AttachmentDescription &description)
{
    EXPECT_EQ(description.samples, vk::SampleCountFlagBits::e1);
    EXPECT_EQ(description.stencilLoadOp, vk::AttachmentLoadOp::eDontCare);
    EXPECT_EQ(description.stencilStoreOp, vk::AttachmentStoreOp::eDontCare);
    EXPECT_EQ(description.flags, vk::AttachmentDescriptionFlags{});
}

TEST(RenderPassHelperUnit, DefaultsAreClearStoreFromUndefined)
{
    const vk::AttachmentDescription description =
      buildAttachmentDescription(vk::Format::eR8G8B8A8Unorm, vk::ImageLayout::eShaderReadOnlyOptimal);

    EXPECT_EQ(description.loadOp, vk::AttachmentLoadOp::eClear);
    EXPECT_EQ(description.storeOp, vk::AttachmentStoreOp::eStore);
    EXPECT_EQ(description.initialLayout, vk::ImageLayout::eUndefined);
    EXPECT_EQ(description.finalLayout, vk::ImageLayout::eShaderReadOnlyOptimal);
    EXPECT_EQ(description.format, vk::Format::eR8G8B8A8Unorm);
    expectEngineWideAttachmentInvariants(description);
}

TEST(RenderPassHelperUnit, MatchesForwardRasterizerColorAttachment)
{
    // Rasterizer::createRenderPass, OFFSCREEN_FORMAT colour target, sampled by
    // the post stage afterwards.
    const vk::AttachmentDescription description =
      buildAttachmentDescription(vk::Format::eR16G16B16A16Sfloat, vk::ImageLayout::eShaderReadOnlyOptimal);

    EXPECT_EQ(description.format, vk::Format::eR16G16B16A16Sfloat);
    EXPECT_EQ(description.loadOp, vk::AttachmentLoadOp::eClear);
    EXPECT_EQ(description.storeOp, vk::AttachmentStoreOp::eStore);
    EXPECT_EQ(description.initialLayout, vk::ImageLayout::eUndefined);
    EXPECT_EQ(description.finalLayout, vk::ImageLayout::eShaderReadOnlyOptimal);
    expectEngineWideAttachmentInvariants(description);
}

TEST(RenderPassHelperUnit, MatchesForwardRasterizerDepthAttachmentWithDiscardedStore)
{
    // Rasterizer::createRenderPass depth: nothing reads it after the pass, so
    // the writeback is explicitly discarded. The ONLY difference from the
    // colour attachment above is storeOp - a regression that dropped the
    // override would still render correctly and cost bandwidth silently.
    const vk::AttachmentDescription description = buildAttachmentDescription(vk::Format::eD32Sfloat,
      vk::ImageLayout::eDepthStencilAttachmentOptimal,
      vk::AttachmentLoadOp::eClear,
      vk::AttachmentStoreOp::eDontCare);

    EXPECT_EQ(description.loadOp, vk::AttachmentLoadOp::eClear);
    EXPECT_EQ(description.storeOp, vk::AttachmentStoreOp::eDontCare);
    EXPECT_EQ(description.initialLayout, vk::ImageLayout::eUndefined);
    EXPECT_EQ(description.finalLayout, vk::ImageLayout::eDepthStencilAttachmentOptimal);
    expectEngineWideAttachmentInvariants(description);
}

TEST(RenderPassHelperUnit, MatchesDeferredGBufferAttachments)
{
    // DeferredRasterizer::createRenderPass builds all five attachments from
    // the defaults; only the format and the final layout differ between them.
    const std::array<vk::AttachmentDescription, 5> attachments = {
        buildAttachmentDescription(vk::Format::eR16G16B16A16Sfloat, vk::ImageLayout::eShaderReadOnlyOptimal),
        buildAttachmentDescription(vk::Format::eR16G16B16A16Sfloat, vk::ImageLayout::eShaderReadOnlyOptimal),
        buildAttachmentDescription(vk::Format::eR8G8B8A8Unorm, vk::ImageLayout::eShaderReadOnlyOptimal),
        buildAttachmentDescription(vk::Format::eR8G8B8A8Unorm, vk::ImageLayout::eShaderReadOnlyOptimal),
        buildAttachmentDescription(vk::Format::eD32Sfloat, vk::ImageLayout::eDepthStencilAttachmentOptimal)
    };

    for (const vk::AttachmentDescription &description : attachments) {
        expectEngineWideAttachmentInvariants(description);
        EXPECT_EQ(description.loadOp, vk::AttachmentLoadOp::eClear);
        EXPECT_EQ(description.storeOp, vk::AttachmentStoreOp::eStore);
        EXPECT_EQ(description.initialLayout, vk::ImageLayout::eUndefined);
    }

    EXPECT_EQ(attachments[4].finalLayout, vk::ImageLayout::eDepthStencilAttachmentOptimal);
    EXPECT_EQ(attachments[0].finalLayout, vk::ImageLayout::eShaderReadOnlyOptimal);
}

TEST(RenderPassHelperUnit, MatchesSkyBoxColorAttachmentLeftForThePostStageToLoad)
{
    // SkyBox::createRenderPass writes the swapchain image first and leaves it
    // in eColorAttachmentOptimal - PostStage's colour attachment then LOADs it
    // out of exactly that layout (see the pairing asserted below).
    const vk::AttachmentDescription sky_box = buildAttachmentDescription(
      vk::Format::eB8G8R8A8Unorm, vk::ImageLayout::eColorAttachmentOptimal);

    const vk::AttachmentDescription post_stage = buildAttachmentDescription(vk::Format::eB8G8R8A8Unorm,
      vk::ImageLayout::ePresentSrcKHR,
      vk::AttachmentLoadOp::eLoad,
      vk::AttachmentStoreOp::eStore,
      vk::ImageLayout::eColorAttachmentOptimal);

    EXPECT_EQ(sky_box.finalLayout, post_stage.initialLayout);
    EXPECT_EQ(sky_box.loadOp, vk::AttachmentLoadOp::eClear);
    EXPECT_EQ(sky_box.initialLayout, vk::ImageLayout::eUndefined);
    expectEngineWideAttachmentInvariants(sky_box);
}

TEST(RenderPassHelperUnit, MatchesPostStageColorAttachmentOverridingEveryDefault)
{
    // The one call site that overrides all three defaulted parameters.
    const vk::AttachmentDescription description = buildAttachmentDescription(vk::Format::eB8G8R8A8Unorm,
      vk::ImageLayout::ePresentSrcKHR,
      vk::AttachmentLoadOp::eLoad,
      vk::AttachmentStoreOp::eStore,
      vk::ImageLayout::eColorAttachmentOptimal);

    EXPECT_EQ(description.loadOp, vk::AttachmentLoadOp::eLoad);
    EXPECT_EQ(description.storeOp, vk::AttachmentStoreOp::eStore);
    EXPECT_EQ(description.initialLayout, vk::ImageLayout::eColorAttachmentOptimal);
    EXPECT_EQ(description.finalLayout, vk::ImageLayout::ePresentSrcKHR);
    expectEngineWideAttachmentInvariants(description);
}

TEST(RenderPassHelperUnit, MatchesPostStageDepthAttachmentThatNeverLeavesItsLayout)
{
    // PostStage's depth attachment is created by an earlier pass and stays in
    // eDepthStencilAttachmentOptimal on both sides.
    const vk::AttachmentDescription description = buildAttachmentDescription(vk::Format::eD32SfloatS8Uint,
      vk::ImageLayout::eDepthStencilAttachmentOptimal,
      vk::AttachmentLoadOp::eClear,
      vk::AttachmentStoreOp::eDontCare,
      vk::ImageLayout::eDepthStencilAttachmentOptimal);

    EXPECT_EQ(description.initialLayout, description.finalLayout);
    EXPECT_EQ(description.storeOp, vk::AttachmentStoreOp::eDontCare);
    // A stencil-carrying depth format must still declare eDontCare for both
    // stencil ops - the engine has no stencil pass.
    expectEngineWideAttachmentInvariants(description);
}

TEST(RenderPassHelperUnit, MatchesCascadedShadowMapDepthAttachmentThatIsSampledLater)
{
    // CascadedShadowMap::createRenderPass: the only depth attachment in the
    // engine that is STORED, because the lighting shader samples it.
    const vk::AttachmentDescription description =
      buildAttachmentDescription(vk::Format::eD32Sfloat, vk::ImageLayout::eShaderReadOnlyOptimal);

    EXPECT_EQ(description.storeOp, vk::AttachmentStoreOp::eStore);
    EXPECT_EQ(description.loadOp, vk::AttachmentLoadOp::eClear);
    EXPECT_EQ(description.initialLayout, vk::ImageLayout::eUndefined);
    EXPECT_EQ(description.finalLayout, vk::ImageLayout::eShaderReadOnlyOptimal);
    expectEngineWideAttachmentInvariants(description);
}

}// namespace
