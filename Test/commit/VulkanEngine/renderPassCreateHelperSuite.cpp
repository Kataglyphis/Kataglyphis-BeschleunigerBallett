// Direct unit coverage for common/RenderPassHelper.hpp's
// buildRenderPassCreateInfo - the helper that replaced five hand-written
// vk::RenderPassCreateInfo blocks across Rasterizer, PostStage,
// DeferredRasterizer, SkyBox and CascadedShadowMap.
//
// SkyBox's original call site hard-coded attachmentCount = 2 instead of
// deriving it from the span it was handed - AttachmentCountIsDerivedFromTheSpan
// below is the regression test for exactly that bug.

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <vulkan/vulkan.hpp>

#include "common/RenderPassHelper.hpp"

using Kataglyphis::buildRenderPassCreateInfo;

namespace {
constexpr std::array<vk::AttachmentDescription, 3> kThreeAttachments{
    vk::AttachmentDescription{}, vk::AttachmentDescription{}, vk::AttachmentDescription{}
};
constexpr std::array<vk::SubpassDescription, 1> kOneSubpass{ vk::SubpassDescription{} };
constexpr std::array<vk::SubpassDependency, 1> kOneDependency{ vk::SubpassDependency{} };
}// namespace

static_assert(buildRenderPassCreateInfo(std::span<const vk::AttachmentDescription>(kThreeAttachments),
                std::span<const vk::SubpassDescription>(kOneSubpass),
                std::span<const vk::SubpassDependency>(kOneDependency))
                .attachmentCount
    == 3U,
  "buildRenderPassCreateInfo must be usable in a constant expression");

namespace {

TEST(RenderPassCreateHelperUnit, AttachmentCountIsDerivedFromTheSpan)
{
    const vk::RenderPassCreateInfo info = buildRenderPassCreateInfo(
      std::span<const vk::AttachmentDescription>(kThreeAttachments),
      std::span<const vk::SubpassDescription>(kOneSubpass), std::span<const vk::SubpassDependency>(kOneDependency));

    EXPECT_EQ(info.attachmentCount, 3U);
}

TEST(RenderPassCreateHelperUnit, SubpassAndDependencyCountsAreDerivedFromTheirSpans)
{
    // The DeferredRasterizer shape: two subpasses, three dependencies.
    const std::array<vk::SubpassDescription, 2> two_subpasses{ vk::SubpassDescription{}, vk::SubpassDescription{} };
    const std::array<vk::SubpassDependency, 3> three_dependencies{
        vk::SubpassDependency{}, vk::SubpassDependency{}, vk::SubpassDependency{}
    };

    const vk::RenderPassCreateInfo info = buildRenderPassCreateInfo(
      std::span<const vk::AttachmentDescription>(kThreeAttachments),
      std::span<const vk::SubpassDescription>(two_subpasses), std::span<const vk::SubpassDependency>(three_dependencies));

    EXPECT_EQ(info.subpassCount, 2U);
    EXPECT_EQ(info.dependencyCount, 3U);
}

TEST(RenderPassCreateHelperUnit, PointersPointAtTheCallersStorage)
{
    const vk::RenderPassCreateInfo info = buildRenderPassCreateInfo(
      std::span<const vk::AttachmentDescription>(kThreeAttachments),
      std::span<const vk::SubpassDescription>(kOneSubpass), std::span<const vk::SubpassDependency>(kOneDependency));

    EXPECT_EQ(info.pAttachments, kThreeAttachments.data());
    EXPECT_EQ(info.pSubpasses, kOneSubpass.data());
    EXPECT_EQ(info.pDependencies, kOneDependency.data());
}

TEST(RenderPassCreateHelperUnit, SingleAttachmentPassStillReportsOne)
{
    // The CascadedShadowMap shape: one depth attachment, no colour.
    const std::array<vk::AttachmentDescription, 1> one_attachment{ vk::AttachmentDescription{} };

    const vk::RenderPassCreateInfo info =
      buildRenderPassCreateInfo(std::span<const vk::AttachmentDescription>(one_attachment),
        std::span<const vk::SubpassDescription>(kOneSubpass), std::span<const vk::SubpassDependency>(kOneDependency));

    EXPECT_EQ(info.attachmentCount, 1U);
    EXPECT_EQ(info.pAttachments, one_attachment.data());
}

TEST(RenderPassCreateHelperUnit, FlagsAndPNextAreDefaultedSoCallersCanChainTheirOwn)
{
    // CascadedShadowMap assigns pNext on the returned value to chain a
    // vk::RenderPassMultiviewCreateInfo - that is only safe if the helper
    // itself leaves both fields untouched.
    const vk::RenderPassCreateInfo info = buildRenderPassCreateInfo(
      std::span<const vk::AttachmentDescription>(kThreeAttachments),
      std::span<const vk::SubpassDescription>(kOneSubpass), std::span<const vk::SubpassDependency>(kOneDependency));

    EXPECT_EQ(info.flags, vk::RenderPassCreateFlags{});
    EXPECT_EQ(info.pNext, nullptr);
}

}// namespace
