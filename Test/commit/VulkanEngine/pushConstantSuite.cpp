// The raster push constant's layout contract.
//
// PushConstantRasterizer is compiled twice: as C++ here and as GLSL inside
// shader.vert/.frag and geometry.vert/.frag, from the same header. The
// pipeline layout declares a range of sizeof(PushConstantRasterizer), so if
// the two ever disagree about the struct's size or field order the GPU reads
// whatever happens to follow - silently, with no validation error, because
// the range is self-consistent on the host side.
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
    // std140/scalar both place a uint immediately after a mat4 here.
    EXPECT_EQ(offsetof(PushConstantRasterizer, objectIndex), 64U);

    // The pipeline layout pushes sizeof(PushConstantRasterizer) bytes. If the
    // struct were sized to the matrix alone, objectIndex would never reach the
    // GPU and every draw would shade with whatever was left in the range.
    EXPECT_GE(sizeof(PushConstantRasterizer), offsetof(PushConstantRasterizer, objectIndex) + sizeof(unsigned int));
}

TEST(PushConstantRasterizerUnit, FitsTheGuaranteedPushConstantBudget)
{
    // Vulkan guarantees only 128 bytes of push constant space. Exceeding it
    // fails pipeline creation on conformant-but-minimal implementations while
    // working fine on a desktop GPU - a bug that only appears on someone
    // else's hardware.
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
