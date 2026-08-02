// Pins FrameSync's device-free contract: a freshly constructed instance
// cycles with frame_sync_count == 1, cleanUp() reports a fully torn-down
// state, and advanceFrame() never divides by zero once frame_sync_count has
// been zeroed by cleanUp() (the state create() leaves behind on failure).

#include <gtest/gtest.h>

#include <vulkan/vulkan.hpp>

#include "common/Globals.hpp"

import kataglyphis.vulkan.frame_sync;

namespace {

TEST(FrameSyncUnit, DefaultStateCyclesWithinOneFrame)
{
    Kataglyphis::FrameSync frame_sync;

    EXPECT_EQ(frame_sync.frameSyncCount(), 1U);
    EXPECT_EQ(frame_sync.currentFrame(), 0U);

    frame_sync.advanceFrame();

    EXPECT_EQ(frame_sync.currentFrame(), 0U);
}

TEST(FrameSyncUnit, CleanUpReportsAFullyTornDownState)
{
    Kataglyphis::FrameSync frame_sync;

    frame_sync.cleanUp(vk::Device{});

    EXPECT_EQ(frame_sync.frameSyncCount(), 0U);
    EXPECT_EQ(frame_sync.currentFrame(), 0U);
    EXPECT_TRUE(frame_sync.inFlightFencesEmpty());
    EXPECT_EQ(frame_sync.imageAvailableCount(), 0U);
    EXPECT_EQ(frame_sync.inFlightFenceCount(), 0U);
    EXPECT_EQ(frame_sync.renderFinishedCount(), 0U);
    EXPECT_EQ(frame_sync.imagesInFlightFenceCount(), 0U);
}

TEST(FrameSyncUnit, AdvanceFrameIsSafeWhenSyncCreationFailed)
{
    Kataglyphis::FrameSync frame_sync;
    frame_sync.cleanUp(vk::Device{});

    // Before the create()-failure guard was added, this modulo-by-zero was
    // undefined behaviour / a crash. Must be a no-op now.
    frame_sync.advanceFrame();

    EXPECT_EQ(frame_sync.currentFrame(), 0U);
}

TEST(FrameSyncUnit, ResetAndSizeSurvivesCleanUpsCounterReset)
{
    Kataglyphis::FrameSync frame_sync;

    // cleanUp() (called internally by resetAndSize()) zeroes frame_sync_count;
    // the sizing computed afterwards must not be clobbered by that reset.
    frame_sync.resetAndSize(vk::Device{}, 3);

    EXPECT_EQ(frame_sync.frameSyncCount(), 3U);
    EXPECT_EQ(frame_sync.imageAvailableCount(), 3U);
    EXPECT_EQ(frame_sync.inFlightFenceCount(), 3U);
    EXPECT_EQ(frame_sync.renderFinishedCount(), 3U);
    EXPECT_EQ(frame_sync.imagesInFlightFenceCount(), 3U);

    Kataglyphis::FrameSync single_image_frame_sync;
    single_image_frame_sync.resetAndSize(vk::Device{}, 1);

    EXPECT_EQ(single_image_frame_sync.frameSyncCount(), 1U);
    EXPECT_EQ(single_image_frame_sync.renderFinishedCount(), 1U);
    EXPECT_EQ(single_image_frame_sync.imagesInFlightFenceCount(), 1U);
}

TEST(FrameSyncUnit, ResetAndSizeClampsToMaxFrameDraws)
{
    Kataglyphis::FrameSync frame_sync;

    uint32_t const large_image_count = static_cast<uint32_t>(Kataglyphis::MAX_FRAME_DRAWS) + 10;
    frame_sync.resetAndSize(vk::Device{}, large_image_count);

    EXPECT_EQ(frame_sync.frameSyncCount(), static_cast<uint32_t>(Kataglyphis::MAX_FRAME_DRAWS));
    EXPECT_EQ(frame_sync.imageAvailableCount(), static_cast<uint32_t>(Kataglyphis::MAX_FRAME_DRAWS));
    EXPECT_EQ(frame_sync.inFlightFenceCount(), static_cast<uint32_t>(Kataglyphis::MAX_FRAME_DRAWS));
    EXPECT_EQ(frame_sync.renderFinishedCount(), large_image_count);
    EXPECT_EQ(frame_sync.imagesInFlightFenceCount(), large_image_count);
}

}// namespace
