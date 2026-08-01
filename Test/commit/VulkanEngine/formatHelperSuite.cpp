// Pins supportsMipmapGeneration's contract: vkCmdBlitImage with
// VK_FILTER_LINEAR needs SAMPLED_IMAGE_FILTER_LINEAR, BLIT_SRC and BLIT_DST
// all present on optimalTilingFeatures, not just the filter bit.

#include <gtest/gtest.h>

#include "common/FormatHelper.hpp"

static_assert(Kataglyphis::supportsMipmapGeneration(vk::FormatFeatureFlagBits::eSampledImageFilterLinear
                                                      | vk::FormatFeatureFlagBits::eBlitSrc
                                                      | vk::FormatFeatureFlagBits::eBlitDst),
  "supportsMipmapGeneration must be usable in a constant expression");

static_assert(Kataglyphis::depthStencilTransitionAspect(vk::Format::eD32SfloatS8Uint)
                == (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil),
  "depthStencilTransitionAspect must be usable in a constant expression");

namespace {

TEST(FormatHelperUnit, AllThreeBlitCapabilitiesAreRequired)
{
    vk::FormatFeatureFlags all = vk::FormatFeatureFlagBits::eSampledImageFilterLinear
                                  | vk::FormatFeatureFlagBits::eBlitSrc | vk::FormatFeatureFlagBits::eBlitDst;
    EXPECT_TRUE(Kataglyphis::supportsMipmapGeneration(all));
}

TEST(FormatHelperUnit, MissingFilterLinearFails)
{
    vk::FormatFeatureFlags flags = vk::FormatFeatureFlagBits::eBlitSrc | vk::FormatFeatureFlagBits::eBlitDst;
    EXPECT_FALSE(Kataglyphis::supportsMipmapGeneration(flags));
}

TEST(FormatHelperUnit, MissingBlitSrcFails)
{
    vk::FormatFeatureFlags flags =
      vk::FormatFeatureFlagBits::eSampledImageFilterLinear | vk::FormatFeatureFlagBits::eBlitDst;
    EXPECT_FALSE(Kataglyphis::supportsMipmapGeneration(flags));
}

TEST(FormatHelperUnit, MissingBlitDstFails)
{
    vk::FormatFeatureFlags flags =
      vk::FormatFeatureFlagBits::eSampledImageFilterLinear | vk::FormatFeatureFlagBits::eBlitSrc;
    EXPECT_FALSE(Kataglyphis::supportsMipmapGeneration(flags));
}

TEST(FormatHelperUnit, EmptyFlagsFail) { EXPECT_FALSE(Kataglyphis::supportsMipmapGeneration(vk::FormatFeatureFlags{})); }

TEST(FormatHelperUnit, CombinedDepthStencilFormatsReportStencil)
{
    EXPECT_TRUE(Kataglyphis::formatHasStencil(vk::Format::eD32SfloatS8Uint));
    EXPECT_TRUE(Kataglyphis::formatHasStencil(vk::Format::eD24UnormS8Uint));
    EXPECT_TRUE(Kataglyphis::formatHasStencil(vk::Format::eD16UnormS8Uint));
}

TEST(FormatHelperUnit, DepthOnlyFormatsDoNotReportStencil)
{
    EXPECT_FALSE(Kataglyphis::formatHasStencil(vk::Format::eD32Sfloat));
    EXPECT_FALSE(Kataglyphis::formatHasStencil(vk::Format::eD16Unorm));
}

TEST(FormatHelperUnit, TransitionAspectAddsStencilOnlyForCombinedFormats)
{
    EXPECT_EQ(Kataglyphis::depthStencilTransitionAspect(vk::Format::eD32Sfloat), vk::ImageAspectFlagBits::eDepth);
    EXPECT_EQ(Kataglyphis::depthStencilTransitionAspect(vk::Format::eD32SfloatS8Uint),
      vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);
}

TEST(FormatHelperUnit, ThePreferredDepthFormatIsStencilFree)
{
    // Head of chooseDepthFormat's preference list: on every device that
    // supports it, depthStencilTransitionAspect is a no-op (eDepth only) and
    // the combined-format fallbacks are the only inputs it actually changes.
    EXPECT_FALSE(Kataglyphis::formatHasStencil(vk::Format::eD32Sfloat));
}

}// namespace
