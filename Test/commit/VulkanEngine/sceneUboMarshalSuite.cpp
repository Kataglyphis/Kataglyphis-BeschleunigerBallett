// Direct unit coverage for common/SceneUboMarshal.hpp - the pure per-frame
// GUI->UBO maths extracted from VulkanRenderer::updateUniforms, which
// otherwise ran 76 lines deep on every frame with nothing able to reach it
// from a test.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <span>

#include "common/SceneUboMarshal.hpp"
#include "common/host_device_shared_vars.hpp"
#include "renderer/SceneUBO.hpp"

using Kataglyphis::aspectRatioOf;
using Kataglyphis::fillSceneUboCascades;
using Kataglyphis::makeVulkanProjection;
using Kataglyphis::VulkanRendererInternals::SceneUBO;

static_assert(aspectRatioOf(1920, 0) == 1.0F, "a zero-height extent must not divide by zero");

namespace {

TEST(SceneUboMarshalUnit, ZeroHeightExtentDoesNotDivideByZero)
{
    EXPECT_FLOAT_EQ(aspectRatioOf(1920, 0), 1.0F);
    EXPECT_FLOAT_EQ(aspectRatioOf(1920, 1080), 1920.0F / 1080.0F);
}

TEST(SceneUboMarshalUnit, ProjectionFlipsYForVulkan)
{
    constexpr float fov = 60.0F;
    constexpr float aspect = 16.0F / 9.0F;
    constexpr float nearPlane = 0.1F;
    constexpr float farPlane = 100.0F;

    const glm::mat4 result = makeVulkanProjection(fov, aspect, nearPlane, farPlane);
    const glm::mat4 reference = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            if (col == 1 && row == 1) {
                EXPECT_FLOAT_EQ(result[col][row], -reference[col][row]);
            } else {
                EXPECT_FLOAT_EQ(result[col][row], reference[col][row]);
            }
        }
    }
}

TEST(SceneUboMarshalUnit, CascadeFillClampsToMaxCascades)
{
    std::array<float, MAX_CASCADES + 2> splitDepths{};
    std::array<glm::mat4, MAX_CASCADES + 2> viewProjMatrices{};
    for (size_t i = 0; i < splitDepths.size(); ++i) {
        splitDepths[i] = static_cast<float>(i) + 1.0F;
        viewProjMatrices[i] = glm::mat4(static_cast<float>(i) + 1.0F);
    }

    SceneUBO ubo{};
    constexpr float kSentinel = -777.0F;
    ubo.cascadeSplits = glm::vec4(kSentinel);

    const uint32_t written = fillSceneUboCascades(ubo,
      std::span<const float>(splitDepths),
      std::span<const glm::mat4>(viewProjMatrices),
      /*shadowsEnabled=*/true);

    EXPECT_EQ(written, static_cast<uint32_t>(MAX_CASCADES));
    EXPECT_EQ(ubo.numCascades, static_cast<uint32_t>(MAX_CASCADES));
    for (int i = 0; i < MAX_CASCADES; ++i) { EXPECT_FLOAT_EQ(ubo.cascadeSplits[i], splitDepths[i]); }
    EXPECT_FLOAT_EQ(ubo.cascadeSplits[MAX_CASCADES], kSentinel)
      << "no write may land past cascadeSplits[MAX_CASCADES - 1]";
}

TEST(SceneUboMarshalUnit, ShadowsDisabledZeroesTheCascadeCountButKeepsTheMatrices)
{
    const std::array<float, 1> splitDepths{ 42.0F };
    const std::array<glm::mat4, 1> viewProjMatrices{ glm::mat4(7.0F) };

    SceneUBO ubo{};

    const uint32_t written = fillSceneUboCascades(ubo,
      std::span<const float>(splitDepths),
      std::span<const glm::mat4>(viewProjMatrices),
      /*shadowsEnabled=*/false);

    EXPECT_EQ(written, 0U);
    EXPECT_EQ(ubo.numCascades, 0U) << "shaders gate on numCascades, which must be zero while shadows are off";
    EXPECT_FLOAT_EQ(ubo.cascadeSplits[0], 42.0F) << "the matrices/splits themselves are still written";
    EXPECT_EQ(ubo.cascadeLightSpaceMatrices[0], glm::mat4(7.0F));
}

TEST(SceneUboMarshalUnit, FewerCascadesThanMaxLeavesTheRestUntouched)
{
    const std::array<float, 1> splitDepths{ 5.0F };
    const std::array<glm::mat4, 1> viewProjMatrices{ glm::mat4(1.0F) };

    SceneUBO ubo{};
    ubo.cascadeSplits = glm::vec4(-1.0F, -2.0F, -3.0F, -4.0F);
    ubo.cascadeLightSpaceMatrices[1] = glm::mat4(-2.0F);
    ubo.cascadeLightSpaceMatrices[2] = glm::mat4(-3.0F);

    fillSceneUboCascades(ubo,
      std::span<const float>(splitDepths),
      std::span<const glm::mat4>(viewProjMatrices),
      /*shadowsEnabled=*/true);

    EXPECT_FLOAT_EQ(ubo.cascadeSplits[0], 5.0F);
    EXPECT_FLOAT_EQ(ubo.cascadeSplits[1], -2.0F);
    EXPECT_FLOAT_EQ(ubo.cascadeSplits[2], -3.0F);
    EXPECT_EQ(ubo.cascadeLightSpaceMatrices[1], glm::mat4(-2.0F));
    EXPECT_EQ(ubo.cascadeLightSpaceMatrices[2], glm::mat4(-3.0F));
}

}// namespace
