// Direct unit coverage for common/PipelineLayoutHelper.hpp's
// buildPipelineLayoutCreateInfo - the helper that replaced nine hand-written
// vk::PipelineLayoutCreateInfo blocks across Clouds, Raytracing, SkyBox,
// Rasterizer, PathTracing, PostStage, CascadedShadowMap and
// DeferredRasterizer (geometry and lighting).
//
// SkyBox's original call site hard-coded setLayoutCount = 2 instead of
// deriving it from the span it was handed - SetLayoutCountIsDerivedFromTheSpan
// below is the regression test for exactly that bug.

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <vulkan/vulkan.hpp>

#include "common/PipelineLayoutHelper.hpp"

using Kataglyphis::buildPipelineLayoutCreateInfo;

namespace {
// vk::DescriptorSetLayout/vk::PushConstantRange's default constructors are
// not constexpr in this vulkan-hpp version - the nullptr_t constructor is,
// so it stands in for "no real handle" wherever a constant expression is
// required below (the same stand-in framebufferHelperSuite.cpp's anonymous
// namespace fixtures use).
constexpr std::array<vk::DescriptorSetLayout, 2> kTwoLayouts{ vk::DescriptorSetLayout(nullptr),
    vk::DescriptorSetLayout(nullptr) };
constexpr std::array<vk::DescriptorSetLayout, 3> kThreeLayouts{ vk::DescriptorSetLayout(nullptr),
    vk::DescriptorSetLayout(nullptr), vk::DescriptorSetLayout(nullptr) };
constexpr std::array<vk::PushConstantRange, 1> kOneRange{ vk::PushConstantRange{} };
constexpr std::array<vk::PushConstantRange, 2> kTwoRanges{ vk::PushConstantRange{}, vk::PushConstantRange{} };
}// namespace

static_assert(
  buildPipelineLayoutCreateInfo(std::span<const vk::DescriptorSetLayout>(kTwoLayouts)).setLayoutCount == 2U,
  "buildPipelineLayoutCreateInfo must be usable in a constant expression");

namespace {

TEST(PipelineLayoutHelperUnit, SetLayoutCountIsDerivedFromTheSpan)
{
    const vk::PipelineLayoutCreateInfo two_layout_info =
      buildPipelineLayoutCreateInfo(std::span<const vk::DescriptorSetLayout>(kTwoLayouts));
    EXPECT_EQ(two_layout_info.setLayoutCount, 2U);

    const vk::PipelineLayoutCreateInfo three_layout_info =
      buildPipelineLayoutCreateInfo(std::span<const vk::DescriptorSetLayout>(kThreeLayouts));
    EXPECT_EQ(three_layout_info.setLayoutCount, 3U);
}

TEST(PipelineLayoutHelperUnit, PushConstantRangeCountIsDerivedFromTheSpan)
{
    const vk::PipelineLayoutCreateInfo one_range_info = buildPipelineLayoutCreateInfo(
      std::span<const vk::DescriptorSetLayout>(kTwoLayouts), std::span<const vk::PushConstantRange>(kOneRange));
    EXPECT_EQ(one_range_info.pushConstantRangeCount, 1U);

    const vk::PipelineLayoutCreateInfo two_range_info = buildPipelineLayoutCreateInfo(
      std::span<const vk::DescriptorSetLayout>(kTwoLayouts), std::span<const vk::PushConstantRange>(kTwoRanges));
    EXPECT_EQ(two_range_info.pushConstantRangeCount, 2U);
}

TEST(PipelineLayoutHelperUnit, OmittedPushConstantsGiveZeroAndNullptr)
{
    // The Clouds / deferred-lighting shape: no push constants at all.
    const vk::PipelineLayoutCreateInfo info =
      buildPipelineLayoutCreateInfo(std::span<const vk::DescriptorSetLayout>(kTwoLayouts));

    EXPECT_EQ(info.pushConstantRangeCount, 0U);
    EXPECT_EQ(info.pPushConstantRanges, nullptr);
}

TEST(PipelineLayoutHelperUnit, PointersPointAtTheCallersStorage)
{
    const vk::PipelineLayoutCreateInfo info = buildPipelineLayoutCreateInfo(
      std::span<const vk::DescriptorSetLayout>(kTwoLayouts), std::span<const vk::PushConstantRange>(kOneRange));

    EXPECT_EQ(info.pSetLayouts, kTwoLayouts.data());
    EXPECT_EQ(info.pPushConstantRanges, kOneRange.data());
}

TEST(PipelineLayoutHelperUnit, FlagsAreDefaulted)
{
    const vk::PipelineLayoutCreateInfo info =
      buildPipelineLayoutCreateInfo(std::span<const vk::DescriptorSetLayout>(kTwoLayouts));

    EXPECT_EQ(info.flags, vk::PipelineLayoutCreateFlags{});
}

}// namespace
