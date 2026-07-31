// Direct unit coverage for kataglyphis.vulkan.sampler_builder::buildSamplerCreateInfo
// - the helper that replaced three drifted, hand-written vk::SamplerCreateInfo
// literals in PostStage/Model/Texture. vk::SamplerCreateInfo is a plain struct,
// so this pins each call site's exact field values field-by-field with no
// device needed, guarding against the fields silently drifting apart again.

#include <gtest/gtest.h>

#include <vulkan/vulkan.hpp>

import kataglyphis.vulkan.sampler_builder;

using Kataglyphis::buildSamplerCreateInfo;

TEST(SamplerBuilderUnit, MatchesPostStageOffscreenSamplerConfiguration)
{
    const vk::SamplerCreateInfo info =
      buildSamplerCreateInfo(vk::Filter::eLinear, vk::SamplerAddressMode::eRepeat, 0.0F, VK_TRUE, 16.0F, vk::BorderColor::eFloatOpaqueBlack);

    EXPECT_EQ(info.magFilter, vk::Filter::eLinear);
    EXPECT_EQ(info.minFilter, vk::Filter::eLinear);
    EXPECT_EQ(info.addressModeU, vk::SamplerAddressMode::eRepeat);
    EXPECT_EQ(info.addressModeV, vk::SamplerAddressMode::eRepeat);
    EXPECT_EQ(info.addressModeW, vk::SamplerAddressMode::eRepeat);
    EXPECT_EQ(info.borderColor, vk::BorderColor::eFloatOpaqueBlack);
    EXPECT_EQ(info.unnormalizedCoordinates, static_cast<vk::Bool32>(VK_FALSE));
    EXPECT_EQ(info.mipmapMode, vk::SamplerMipmapMode::eLinear);
    EXPECT_FLOAT_EQ(info.mipLodBias, 0.0F);
    EXPECT_FLOAT_EQ(info.minLod, 0.0F);
    EXPECT_FLOAT_EQ(info.maxLod, 0.0F);
    EXPECT_EQ(info.anisotropyEnable, static_cast<vk::Bool32>(VK_TRUE));
    EXPECT_FLOAT_EQ(info.maxAnisotropy, 16.0F);
}

TEST(SamplerBuilderUnit, MatchesModelSamplerConfigurationWithMipLevelAsMaxLod)
{
    const vk::SamplerCreateInfo info =
      buildSamplerCreateInfo(vk::Filter::eLinear, vk::SamplerAddressMode::eRepeat, 4.0F, VK_FALSE, 1.0F, vk::BorderColor::eFloatOpaqueBlack);

    EXPECT_EQ(info.magFilter, vk::Filter::eLinear);
    EXPECT_EQ(info.minFilter, vk::Filter::eLinear);
    EXPECT_EQ(info.addressModeU, vk::SamplerAddressMode::eRepeat);
    EXPECT_EQ(info.borderColor, vk::BorderColor::eFloatOpaqueBlack);
    // Model.cpp passes the texture's own mip level as maxLod (unlike PostStage's
    // fixed 0.0F) - this is the one field the three sites genuinely disagree on.
    EXPECT_FLOAT_EQ(info.maxLod, 4.0F);
    EXPECT_EQ(info.anisotropyEnable, static_cast<vk::Bool32>(VK_FALSE));
    EXPECT_FLOAT_EQ(info.maxAnisotropy, 1.0F);
}

TEST(SamplerBuilderUnit, MatchesTextureSamplerConfigurationWithAnisotropyHardDisabled)
{
    const vk::SamplerCreateInfo info =
      buildSamplerCreateInfo(vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, 5.0F, VK_FALSE, 1.0F, vk::BorderColor::eIntOpaqueBlack);

    EXPECT_EQ(info.magFilter, vk::Filter::eNearest);
    EXPECT_EQ(info.minFilter, vk::Filter::eNearest);
    EXPECT_EQ(info.addressModeU, vk::SamplerAddressMode::eClampToEdge);
    EXPECT_EQ(info.addressModeV, vk::SamplerAddressMode::eClampToEdge);
    EXPECT_EQ(info.addressModeW, vk::SamplerAddressMode::eClampToEdge);
    // Texture::createTextureSampler hard-disables anisotropy regardless of
    // device support - a known asymmetry with the other two sites, preserved
    // by the caller rather than the shared builder.
    EXPECT_EQ(info.anisotropyEnable, static_cast<vk::Bool32>(VK_FALSE));
    EXPECT_FLOAT_EQ(info.maxAnisotropy, 1.0F);
    EXPECT_EQ(info.borderColor, vk::BorderColor::eIntOpaqueBlack);
    EXPECT_FLOAT_EQ(info.maxLod, 5.0F);
    // compareEnable/compareOp are left at the struct's zero-initialized default
    // by the builder itself; Texture.cpp sets compareOp = eAlways afterwards,
    // which this test does not model since that happens outside the helper.
}
