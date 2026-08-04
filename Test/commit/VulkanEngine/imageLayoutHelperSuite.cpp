// Pins accessFlagsForImageLayout/pipelineStageForLayout's contract: every
// layout the engine actually transitions through must map to a specific
// access mask and stage, and an unhandled layout must fall through to the
// documented "no access, bottom of pipe" default rather than silently
// stalling or racing.

#include <gtest/gtest.h>

#include "common/ImageLayoutHelper.hpp"

static_assert(Kataglyphis::accessFlagsForImageLayout(vk::ImageLayout::eUndefined) == vk::AccessFlags{},
  "accessFlagsForImageLayout must be usable in a constant expression");

static_assert(Kataglyphis::pipelineStageForLayout(vk::ImageLayout::eUndefined)
                == vk::PipelineStageFlagBits::eTopOfPipe,
  "pipelineStageForLayout must be usable in a constant expression");

namespace {

TEST(ImageLayoutHelperUnit, UndefinedHasNoAccessAndTopOfPipe)
{
    EXPECT_EQ(Kataglyphis::accessFlagsForImageLayout(vk::ImageLayout::eUndefined), vk::AccessFlags{});
    EXPECT_EQ(Kataglyphis::pipelineStageForLayout(vk::ImageLayout::eUndefined), vk::PipelineStageFlagBits::eTopOfPipe);
}

TEST(ImageLayoutHelperUnit, PreinitializedIsHostWrite)
{
    EXPECT_EQ(Kataglyphis::accessFlagsForImageLayout(vk::ImageLayout::ePreinitialized), vk::AccessFlagBits::eHostWrite);
    EXPECT_EQ(Kataglyphis::pipelineStageForLayout(vk::ImageLayout::ePreinitialized), vk::PipelineStageFlagBits::eHost);
}

TEST(ImageLayoutHelperUnit, TransferSrcIsTransferRead)
{
    EXPECT_EQ(Kataglyphis::accessFlagsForImageLayout(vk::ImageLayout::eTransferSrcOptimal), vk::AccessFlagBits::eTransferRead);
    EXPECT_EQ(Kataglyphis::pipelineStageForLayout(vk::ImageLayout::eTransferSrcOptimal), vk::PipelineStageFlagBits::eTransfer);
}

TEST(ImageLayoutHelperUnit, TransferDstIsTransferWrite)
{
    EXPECT_EQ(Kataglyphis::accessFlagsForImageLayout(vk::ImageLayout::eTransferDstOptimal), vk::AccessFlagBits::eTransferWrite);
    EXPECT_EQ(Kataglyphis::pipelineStageForLayout(vk::ImageLayout::eTransferDstOptimal), vk::PipelineStageFlagBits::eTransfer);
}

TEST(ImageLayoutHelperUnit, ColorAttachmentOptimalIsColorAttachmentWrite)
{
    EXPECT_EQ(Kataglyphis::accessFlagsForImageLayout(vk::ImageLayout::eColorAttachmentOptimal),
      vk::AccessFlagBits::eColorAttachmentWrite);
    EXPECT_EQ(Kataglyphis::pipelineStageForLayout(vk::ImageLayout::eColorAttachmentOptimal),
      vk::PipelineStageFlagBits::eColorAttachmentOutput);
}

TEST(ImageLayoutHelperUnit, DepthStencilAttachmentOptimalWidensStageToAllCommands)
{
    // Deliberately eAllCommands rather than the narrower eEarlyFragmentTests -
    // that is what lets this transition be recorded on a non-graphics queue.
    EXPECT_EQ(Kataglyphis::accessFlagsForImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal),
      vk::AccessFlagBits::eDepthStencilAttachmentWrite);
    EXPECT_EQ(Kataglyphis::pipelineStageForLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal),
      vk::PipelineStageFlagBits::eAllCommands);
}

TEST(ImageLayoutHelperUnit, ShaderReadOnlyOptimalWidensStageToAllCommands)
{
    // Deliberately eAllCommands rather than the narrower eFragmentShader - same
    // reason as the depth/stencil case above.
    EXPECT_EQ(Kataglyphis::accessFlagsForImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal),
      vk::AccessFlagBits::eShaderRead);
    EXPECT_EQ(Kataglyphis::pipelineStageForLayout(vk::ImageLayout::eShaderReadOnlyOptimal),
      vk::PipelineStageFlagBits::eAllCommands);
}

TEST(ImageLayoutHelperUnit, GeneralIsTheFiveBitAccessUnionAndAllCommands)
{
    vk::AccessFlags const expected = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
                                      | vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eTransferRead
                                      | vk::AccessFlagBits::eTransferWrite;
    EXPECT_EQ(Kataglyphis::accessFlagsForImageLayout(vk::ImageLayout::eGeneral), expected);
    EXPECT_EQ(Kataglyphis::pipelineStageForLayout(vk::ImageLayout::eGeneral), vk::PipelineStageFlagBits::eAllCommands);
}

TEST(ImageLayoutHelperUnit, UnhandledLayoutFallsThroughToEmptyAccessAndBottomOfPipe)
{
    EXPECT_EQ(Kataglyphis::accessFlagsForImageLayout(vk::ImageLayout::ePresentSrcKHR), vk::AccessFlags{});
    EXPECT_EQ(Kataglyphis::pipelineStageForLayout(vk::ImageLayout::ePresentSrcKHR), vk::PipelineStageFlagBits::eBottomOfPipe);
}

// The SkyBox regression: assert the helper reproduces exactly the first
// barrier SkyBox::uploadCubeMapFaces used to write by hand before it was
// switched to VulkanImage::transitionImageLayout.
TEST(ImageLayoutHelperUnit, ReproducesSkyBoxFirstBarrier)
{
    EXPECT_EQ(Kataglyphis::accessFlagsForImageLayout(vk::ImageLayout::eUndefined), vk::AccessFlags{});
    EXPECT_EQ(Kataglyphis::accessFlagsForImageLayout(vk::ImageLayout::eTransferDstOptimal), vk::AccessFlagBits::eTransferWrite);
    EXPECT_EQ(Kataglyphis::pipelineStageForLayout(vk::ImageLayout::eUndefined), vk::PipelineStageFlagBits::eTopOfPipe);
    EXPECT_EQ(Kataglyphis::pipelineStageForLayout(vk::ImageLayout::eTransferDstOptimal), vk::PipelineStageFlagBits::eTransfer);
}

// Texture::generateMipMaps publishes every mip level to eShaderReadOnlyOptimal
// and is read back by the raster fragment shaders, raytrace.rchit.slang's ray
// queries and the path_tracing.slang compute kernel. eComputeShader and
// eRayTracingShaderKHR are distinct bits from eAllCommands (0x00000800 and
// 0x00200000 versus 0x00010000), so they cannot be pulled out of the mask
// with a bitwise AND - eAllCommands is the sentinel the Vulkan spec defines
// to mean "synchronize against every stage", which is what actually covers
// those two readers as well as eFragmentShader. Pin that the helper still
// answers this sentinel rather than the narrower eFragmentShader alone.
TEST(ImageLayoutHelperUnit, ShaderReadOnlyDestinationCoversComputeAndRayTracing)
{
    const vk::PipelineStageFlags stage = Kataglyphis::pipelineStageForLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    EXPECT_EQ(stage, vk::PipelineStageFlagBits::eAllCommands);
    EXPECT_NE(stage, vk::PipelineStageFlagBits::eFragmentShader);
}

}// namespace
