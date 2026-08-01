// Pins supportsMipmapGeneration's contract: vkCmdBlitImage with
// VK_FILTER_LINEAR needs SAMPLED_IMAGE_FILTER_LINEAR, BLIT_SRC and BLIT_DST
// all present on optimalTilingFeatures, not just the filter bit.

#include <gtest/gtest.h>

#include "common/FormatHelper.hpp"

static_assert(Kataglyphis::supportsMipmapGeneration(vk::FormatFeatureFlagBits::eSampledImageFilterLinear
                                                      | vk::FormatFeatureFlagBits::eBlitSrc
                                                      | vk::FormatFeatureFlagBits::eBlitDst),
  "supportsMipmapGeneration must be usable in a constant expression");

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

}// namespace
