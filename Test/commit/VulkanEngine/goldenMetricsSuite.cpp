// Unit tests for the pixel-metric oracles shared across goldenRenderSuite.cpp
// (GoldenMetrics.hpp). These are pure CPU functions over RGBA8 buffers - no
// Vulkan, no engine, no GPU - so they run everywhere the CPU-only suites do.
//
// They exist because the metrics ARE the oracle every golden test trusts:
// BACKLOG.md's "Caution learned the hard way" note records two confident
// wrong calls that traced back to an untested pixel classifier. Pinning the
// metrics themselves down here means a future change to them is caught by a
// fast CPU test, not by a GPU golden failing (or silently passing) for the
// wrong reason.

#include <gtest/gtest.h>

#include "GoldenMetrics.hpp"

#include <cstdint>
#include <vector>

namespace {

using Kataglyphis::Test::GoldenMetrics::Crop;
using Kataglyphis::Test::GoldenMetrics::detail_fraction;
using Kataglyphis::Test::GoldenMetrics::swung_fraction;

constexpr uint32_t WIDTH = 8;
constexpr uint32_t HEIGHT = 8;

std::vector<uint8_t> flat_frame(uint8_t level)
{
    return std::vector<uint8_t>(static_cast<size_t>(WIDTH) * HEIGHT * 4U, level);
}

// Alternating columns of black/white, opaque alpha.
std::vector<uint8_t> checkerboard_frame()
{
    std::vector<uint8_t> rgba(static_cast<size_t>(WIDTH) * HEIGHT * 4U, 0U);
    for (uint32_t y = 0; y < HEIGHT; ++y) {
        for (uint32_t x = 0; x < WIDTH; ++x) {
            const uint8_t level = (x % 2U == 0U) ? 0U : 255U;
            const size_t base = (static_cast<size_t>(y) * WIDTH + x) * 4U;
            rgba[base] = level;
            rgba[base + 1U] = level;
            rgba[base + 2U] = level;
            rgba[base + 3U] = 255U;
        }
    }
    return rgba;
}

Crop full_frame_crop() { return Crop{0U, WIDTH, 0U, HEIGHT}; }

// detail_fraction reads each pixel's RIGHT-HAND neighbour, so a crop whose x1
// reaches the frame width would read one pixel past the last row on the
// bottom-right corner. Every real caller's crop already excludes the
// rightmost column (see GoldenMetrics.hpp's panel_free_crop/card_crop); this
// helper keeps these tests to the same contract instead of relying on
// out-of-bounds vector access being merely undefined rather than caught.
Crop detail_safe_crop() { return Crop{0U, WIDTH - 1U, 0U, HEIGHT}; }

} // namespace

TEST(GoldenMetrics, SwungFractionIsZeroForIdenticalFrames)
{
    const std::vector<uint8_t> frame = checkerboard_frame();
    EXPECT_DOUBLE_EQ(swung_fraction(frame, frame, WIDTH, HEIGHT, full_frame_crop()), 0.0);
}

TEST(GoldenMetrics, SwungFractionIsOneForFullyDifferentFrames)
{
    const std::vector<uint8_t> black = flat_frame(0U);
    const std::vector<uint8_t> white = flat_frame(255U);
    EXPECT_DOUBLE_EQ(swung_fraction(black, white, WIDTH, HEIGHT, full_frame_crop()), 1.0);
}

TEST(GoldenMetrics, DetailFractionIsZeroForFlatImage)
{
    const std::vector<uint8_t> frame = flat_frame(128U);
    EXPECT_DOUBLE_EQ(detail_fraction(frame, WIDTH, HEIGHT, detail_safe_crop()), 0.0);
}

TEST(GoldenMetrics, DetailFractionIsHighForCheckerboard)
{
    const std::vector<uint8_t> frame = checkerboard_frame();
    // Every column boundary within the crop is an edge, so the fraction is
    // close to (but not exactly) 1.0.
    EXPECT_GT(detail_fraction(frame, WIDTH, HEIGHT, detail_safe_crop()), 0.4);
}
