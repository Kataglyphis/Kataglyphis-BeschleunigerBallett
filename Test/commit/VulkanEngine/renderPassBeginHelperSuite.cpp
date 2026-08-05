// Direct unit coverage for common/RenderPassHelper.hpp's
// buildRenderPassBeginInfo - the helper that replaced five hand-written
// vk::RenderPassBeginInfo blocks across Rasterizer, PostStage,
// DeferredRasterizer, SkyBox and CascadedShadowMap.
//
// Two of the five original call sites hard-coded clearValueCount as a
// literal instead of deriving it from the clear-value array they were
// handed - ClearValueCountIsDerivedFromTheSpan below is the regression test
// for exactly that bug.

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <vulkan/vulkan.hpp>

#include "common/RenderPassHelper.hpp"

using Kataglyphis::buildRenderPassBeginInfo;

namespace {
// vk::RenderPass/vk::Framebuffer's default constructors are not constexpr in
// this vulkan-hpp version - the nullptr_t constructor is, so it stands in for
// "no real handle" wherever a constant expression is required below.
constexpr std::array<vk::ClearValue, 3> kThreeClearValues{
    vk::ClearValue{}, vk::ClearValue{}, vk::ClearValue{}
};
}// namespace

static_assert(buildRenderPassBeginInfo(vk::RenderPass(nullptr), vk::Framebuffer(nullptr), vk::Extent2D{ 1920, 1080 },
                std::span<const vk::ClearValue>(kThreeClearValues))
                .clearValueCount
    == 3U,
  "buildRenderPassBeginInfo must be usable in a constant expression");

namespace {

TEST(RenderPassBeginHelperUnit, ClearValueCountIsDerivedFromTheSpan)
{
    const vk::RenderPassBeginInfo info = buildRenderPassBeginInfo(vk::RenderPass{}, vk::Framebuffer{},
      vk::Extent2D{ 800, 600 }, std::span<const vk::ClearValue>(kThreeClearValues));

    EXPECT_EQ(info.clearValueCount, 3U);
}

TEST(RenderPassBeginHelperUnit, EmptySpanYieldsZeroClearValueCountAndNullPointer)
{
    const vk::RenderPassBeginInfo info = buildRenderPassBeginInfo(
      vk::RenderPass{}, vk::Framebuffer{}, vk::Extent2D{ 800, 600 }, std::span<const vk::ClearValue>{});

    EXPECT_EQ(info.clearValueCount, 0U);
    EXPECT_EQ(info.pClearValues, nullptr);
}

TEST(RenderPassBeginHelperUnit, PClearValuesPointsAtTheCallersStorage)
{
    const vk::RenderPassBeginInfo info = buildRenderPassBeginInfo(vk::RenderPass{}, vk::Framebuffer{},
      vk::Extent2D{ 800, 600 }, std::span<const vk::ClearValue>(kThreeClearValues));

    EXPECT_EQ(info.pClearValues, kThreeClearValues.data());
}

TEST(RenderPassBeginHelperUnit, RenderAreaOffsetIsAlwaysZero)
{
    const vk::RenderPassBeginInfo info = buildRenderPassBeginInfo(vk::RenderPass{}, vk::Framebuffer{},
      vk::Extent2D{ 800, 600 }, std::span<const vk::ClearValue>(kThreeClearValues));

    EXPECT_EQ(info.renderArea.offset.x, 0);
    EXPECT_EQ(info.renderArea.offset.y, 0);
}

TEST(RenderPassBeginHelperUnit, RenderAreaExtentComesFromTheExtent)
{
    const vk::Extent2D extent{ 1920, 1080 };
    const vk::RenderPassBeginInfo info = buildRenderPassBeginInfo(
      vk::RenderPass{}, vk::Framebuffer{}, extent, std::span<const vk::ClearValue>(kThreeClearValues));

    EXPECT_EQ(info.renderArea.extent.width, 1920U);
    EXPECT_EQ(info.renderArea.extent.height, 1080U);
}

}// namespace
