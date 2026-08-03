// SceneUBO's host/shader layout contract: hand-mirrored in
// Resources/ShadersSlang/common/scene_types.slang (struct SceneUBO). Slang
// compiles ConstantBuffer<SceneUBO> as std140 (emitted type SceneUBO_std140),
// where a vec4 has base alignment 16; glm's plain (non-aligned) vec4 gives
// the host struct no such padding by default, so cascadeSplits and every
// member after it drift unless the host struct pads explicitly. If any
// expectation below fails at runtime, that is a REAL C++/Slang layout
// finding - investigate against the .slang twin and report it, do not adjust
// a number to make the test pass.

#include <gtest/gtest.h>

#include <cstddef>
#include <glm/glm.hpp>

#include "renderer/SceneUBO.hpp"

namespace {
using Kataglyphis::VulkanRendererInternals::SceneUBO;
}// namespace

TEST(SceneUboLayoutUnit, MatchesTheCompiledStd140Block)
{
    EXPECT_EQ(offsetof(SceneUBO, dirLight), 0U);
    EXPECT_EQ(offsetof(SceneUBO, dirLight.direction), 0U);
    EXPECT_EQ(offsetof(SceneUBO, dirLight.color), 16U);
    EXPECT_EQ(offsetof(SceneUBO, pcfRadius), 32U);
    EXPECT_EQ(offsetof(SceneUBO, cascadedShadowIntensity), 36U);
    EXPECT_EQ(offsetof(SceneUBO, numCascades), 40U);
    EXPECT_EQ(offsetof(SceneUBO, cascadeSplits), 48U);
    EXPECT_EQ(offsetof(SceneUBO, cascadeLightSpaceMatrices), 64U);
    EXPECT_EQ(offsetof(SceneUBO, view_dir), 256U);
    EXPECT_EQ(offsetof(SceneUBO, cam_pos), 272U);
    EXPECT_EQ(offsetof(SceneUBO, cloudLightMarch), 288U);
    EXPECT_EQ(offsetof(SceneUBO, cloudMeshScale), 304U);
    EXPECT_EQ(offsetof(SceneUBO, cloudMeshOffset), 320U);
    EXPECT_EQ(offsetof(SceneUBO, cloudParameters), 336U);
    EXPECT_EQ(sizeof(glm::mat4) * MAX_CASCADES, 192U);
    EXPECT_EQ(sizeof(SceneUBO), 352U);
}
