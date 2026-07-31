// The raster push constant's layout contract.
//
// PushConstantRasterizer is hand-mirrored in three places: as C++ here, and
// as Slang inside Resources/ShadersSlang/common/push_constants.slang
// (imported by both rasterizer.slang and deferred.slang, so those two agree
// with each other automatically - only the C++/Slang pair can still drift).
// The pipeline layout declares a range of sizeof(PushConstantRasterizer), so
// if the C++ and Slang copies ever disagree about the struct's size or field
// order the GPU reads whatever happens to follow - silently, with no
// validation error, because the range is self-consistent on the host side.
//
// That is exactly how the single-object bug hid: the shaders indexed
// object_description.i[0] unconditionally, so an index field being absent
// or misplaced would have made no observable difference until a second model
// existed.

#include <gtest/gtest.h>

#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "renderer/pushConstants/PushConstantRasterizer.hpp"

namespace {
using Kataglyphis::VulkanRendererInternals::PushConstantRasterizer;
}// namespace

TEST(PushConstantRasterizerUnit, ModelMatrixComesFirstAtOffsetZero)
{
    // The vertex shaders read pc_raster.model; GLSL lays the block out with
    // model at offset 0. A field inserted before it shifts every draw's
    // transform.
    EXPECT_EQ(offsetof(PushConstantRasterizer, model), 0U);
    EXPECT_EQ(sizeof(glm::mat4), 64U);
}

TEST(PushConstantRasterizerUnit, ObjectIndexFollowsTheMatrixAndIsCovered)
{
    // model (64 bytes) is followed by invModelRows[3] (3 * 16 = 48 bytes),
    // then objectIndex.
    EXPECT_EQ(offsetof(PushConstantRasterizer, objectIndex), 112U);

    // The pipeline layout pushes sizeof(PushConstantRasterizer) bytes. If the
    // struct were sized to the matrices alone, objectIndex would never reach
    // the GPU and every draw would shade with whatever was left in the range.
    EXPECT_GE(sizeof(PushConstantRasterizer), offsetof(PushConstantRasterizer, objectIndex) + sizeof(unsigned int));
}

TEST(PushConstantRasterizerUnit, FitsTheGuaranteedPushConstantBudget)
{
    // Vulkan guarantees only 128 bytes of push constant space. Exceeding it
    // fails pipeline creation on conformant-but-minimal implementations while
    // working fine on a desktop GPU - a bug that only appears on someone
    // else's hardware. A full mat4 invModel (instead of the row-packed
    // invModelRows[3]) would push this to 132 and fail this test.
    EXPECT_LE(sizeof(PushConstantRasterizer), 128U);
}

TEST(PushConstantRasterizerUnit, CarriesTheValuesItIsGiven)
{
    PushConstantRasterizer push{};
    push.model = glm::scale(glm::mat4(1.0F), glm::vec3(3.0F));
    push.objectIndex = 7U;

    EXPECT_EQ(push.objectIndex, 7U);
    EXPECT_FLOAT_EQ(push.model[0][0], 3.0F);
    EXPECT_NE(push.model, glm::mat4(1.0F));
}

TEST(PushConstantRasterizerUnit, InvModelRowsRoundTripANonUniformScale)
{
    // Mirrors the extraction in Rasterizer.cpp / DeferredRasterizer.cpp:
    // GLM is column-major, so row i is (m[0][i], m[1][i], m[2][i], 0).
    const glm::mat4 model = glm::scale(glm::mat4(1.0F), glm::vec3(1.0F, 2.0F, 3.0F));
    const glm::mat4 inv_transpose_model = glm::inverse(glm::transpose(model));

    PushConstantRasterizer push{};
    for (int row = 0; row < 3; ++row) {
        push.invModelRows[row] =
          glm::vec4(inv_transpose_model[0][row], inv_transpose_model[1][row], inv_transpose_model[2][row], 0.0F);
    }

    // A diagonal scale matrix is its own transpose, so the inverse-transpose
    // of diag(1, 2, 3) is diag(1, 1/2, 1/3) - exercised here as the row
    // extraction under test, not just the math.
    EXPECT_FLOAT_EQ(push.invModelRows[0][0], 1.0F);
    EXPECT_FLOAT_EQ(push.invModelRows[1][1], 0.5F);
    EXPECT_FLOAT_EQ(push.invModelRows[2][2], 1.0F / 3.0F);

    // Off-diagonal entries stay zero and w is always zero (direction, not point).
    EXPECT_FLOAT_EQ(push.invModelRows[0][1], 0.0F);
    EXPECT_FLOAT_EQ(push.invModelRows[0][2], 0.0F);
    EXPECT_FLOAT_EQ(push.invModelRows[0][3], 0.0F);
    EXPECT_FLOAT_EQ(push.invModelRows[1][3], 0.0F);
    EXPECT_FLOAT_EQ(push.invModelRows[2][3], 0.0F);
}
