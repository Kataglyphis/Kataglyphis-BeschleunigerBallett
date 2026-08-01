// Pins FrameSync's device-free contract: a freshly constructed instance
// cycles with frame_sync_count == 1, cleanUp() reports a fully torn-down
// state, and advanceFrame() never divides by zero once frame_sync_count has
// been zeroed by cleanUp() (the state create() leaves behind on failure).

#include <gtest/gtest.h>

#include <vulkan/vulkan.hpp>

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

}// namespace
