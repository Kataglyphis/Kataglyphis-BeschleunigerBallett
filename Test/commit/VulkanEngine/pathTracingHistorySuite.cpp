// Direct unit coverage for renderer/PathTracingHistory.hpp's
// PathTracingHistoryKey - the value that decides whether the path tracer's
// temporal accumulation history is still valid. Extracted as part of making
// the light part of that key: a light change used to keep blending frames
// lit by the old light into the running mean until the camera happened to
// move (see the BACKLOG entry and VulkanRenderer.cpp's
// recordRaytracingOrPathTracing).

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "renderer/PathTracingHistory.hpp"

using Kataglyphis::VulkanRendererInternals::PathTracingHistoryKey;

namespace {

auto make_key() -> PathTracingHistoryKey
{
    return PathTracingHistoryKey{
        .view = glm::mat4(1.0F),
        .lightDirection = glm::vec4(0.0F, -1.0F, 0.0F, 1.0F),
        .lightColorAndRadiance = glm::vec4(1.0F, 1.0F, 1.0F, 2.0F),
        .samplesPerPixel = 4,
        .maxBounces = 3,
    };
}

}// namespace

TEST(PathTracingHistoryUnit, IdenticalKeysCompareEqual)
{
    const PathTracingHistoryKey a = make_key();
    const PathTracingHistoryKey b = make_key();

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(PathTracingHistoryUnit, ALightDirectionChangeInvalidatesTheHistory)
{
    const PathTracingHistoryKey a = make_key();
    PathTracingHistoryKey b = make_key();
    b.lightDirection = glm::vec4(1.0F, 0.0F, 0.0F, 1.0F);

    EXPECT_TRUE(a != b);
}

// The .w channel of lightColorAndRadiance carries the light's radiance
// (see DirectionalLightData::color in SceneUBO.hpp) - it is easy to drop
// when only comparing rgb, so it gets its own test.
TEST(PathTracingHistoryUnit, ARadianceChangeInvalidatesTheHistory)
{
    const PathTracingHistoryKey a = make_key();
    PathTracingHistoryKey b = make_key();
    b.lightColorAndRadiance.w = a.lightColorAndRadiance.w + 1.0F;

    EXPECT_TRUE(a != b);
}

TEST(PathTracingHistoryUnit, ACameraMoveStillInvalidatesTheHistory)
{
    const PathTracingHistoryKey a = make_key();
    PathTracingHistoryKey b = make_key();
    b.view = glm::translate(glm::mat4(1.0F), glm::vec3(1.0F, 0.0F, 0.0F));

    EXPECT_TRUE(a != b);
}
