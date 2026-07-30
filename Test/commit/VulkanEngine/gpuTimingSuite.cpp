// CPU-only tests for GpuTimingSubsystem::GpuPassAverage, the rolling-window
// mean that smooths per-pass GPU milliseconds for the GUI. It is a pure
// struct with no device dependency, so it is testable without a GPU.

#include <gtest/gtest.h>

import kataglyphis.vulkan.gpu_timing;

namespace {
using Kataglyphis::GpuTimingSubsystem;
}// namespace

TEST(GpuTimingUnit, AverageConvergesToMean)
{
    GpuTimingSubsystem::GpuPassAverage average;
    float last = 0.0F;
    for (uint32_t i = 0; i < GpuTimingSubsystem::GpuPassAverage::WINDOW; i++) { last = average.add(10.0F); }
    EXPECT_FLOAT_EQ(last, 10.0F);
}

TEST(GpuTimingUnit, WindowRollsOldestSample)
{
    GpuTimingSubsystem::GpuPassAverage average;
    for (uint32_t i = 0; i < GpuTimingSubsystem::GpuPassAverage::WINDOW; i++) { average.add(10.0F); }

    // A 31st sample evicts the oldest (10.0): mean becomes (290 + 20) / 30.
    const float mean = average.add(20.0F);
    constexpr float EXPECTED = (290.0F + 20.0F) / 30.0F;
    EXPECT_NEAR(mean, EXPECTED, 1e-4F);
}

TEST(GpuTimingUnit, ResetClearsHistory)
{
    GpuTimingSubsystem::GpuPassAverage average;
    for (uint32_t i = 0; i < GpuTimingSubsystem::GpuPassAverage::WINDOW; i++) { average.add(10.0F); }

    average.reset();
    EXPECT_EQ(average.count, 0U);

    // A fresh sample after reset must not be blended with the pre-reset
    // history - a struct that never evicts would report 10.0 forever even
    // after reset() and this single sample.
    const float mean = average.add(20.0F);
    EXPECT_FLOAT_EQ(mean, 20.0F);
}
