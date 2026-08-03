// CPU-only tests for GpuTimingSubsystem::GpuPassAverage, the rolling-window
// mean that smooths per-pass GPU milliseconds for the GUI. It is a pure
// struct with no device dependency, so it is testable without a GPU.

#include <cstring>

#include <gtest/gtest.h>

import kataglyphis.vulkan.gpu_timing;
import kataglyphis.vulkan.gui_renderer_shared_vars;

namespace {
using Kataglyphis::GpuTimingSubsystem;
namespace FrontendShared = Kataglyphis::VulkanRendererInternals::FrontendShared;
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

// Covers the tables in GUIRendererSharedVars.ixx that
// GPU_TIMED_PASS_COUNT/GpuTimedPass::Count/GpuTimings::pass_ms all derive
// from - a zero-filled or mismatched table compiles fine but produces a
// nullptr pass name or a 0.000 ms reading that silently means "no sample".
TEST(GpuTimingTablesUnit, EveryPassHasBothADisplayAndAnExportName)
{
    for (int i = 0; i < FrontendShared::GPU_TIMED_PASS_COUNT; i++) {
        ASSERT_NE(FrontendShared::GPU_TIMED_PASS_NAMES[static_cast<size_t>(i)], nullptr);
        EXPECT_STRNE(FrontendShared::GPU_TIMED_PASS_NAMES[static_cast<size_t>(i)], "");
        ASSERT_NE(FrontendShared::GPU_TIMED_PASS_EXPORT_NAMES[static_cast<size_t>(i)], nullptr);
        EXPECT_STRNE(FrontendShared::GPU_TIMED_PASS_EXPORT_NAMES[static_cast<size_t>(i)], "");
    }
}

TEST(GpuTimingTablesUnit, ExportNamesAreUniqueAndContainNoSpaces)
{
    for (int i = 0; i < FrontendShared::GPU_TIMED_PASS_COUNT; i++) {
        const char *name = FrontendShared::GPU_TIMED_PASS_EXPORT_NAMES[static_cast<size_t>(i)];
        EXPECT_EQ(std::strchr(name, ' '), nullptr) << "export name '" << name << "' contains a space";
        for (int j = i + 1; j < FrontendShared::GPU_TIMED_PASS_COUNT; j++) {
            EXPECT_STRNE(name, FrontendShared::GPU_TIMED_PASS_EXPORT_NAMES[static_cast<size_t>(j)])
              << "export names at " << i << " and " << j << " collide";
        }
    }
}

TEST(GpuTimingTablesUnit, DefaultTimingsReportNoSampleForEveryPass)
{
    const FrontendShared::GpuTimings timings;
    for (float sample : timings.pass_ms) { EXPECT_LT(sample, 0.0F); }
}

TEST(GpuTimingTablesUnit, PassCountMatchesTheEnum)
{
    EXPECT_EQ(static_cast<int>(FrontendShared::GpuTimedPass::Count), FrontendShared::GPU_TIMED_PASS_COUNT);
}
