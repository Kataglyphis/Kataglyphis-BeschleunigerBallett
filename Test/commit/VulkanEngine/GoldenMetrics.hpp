// Shared pixel-metric oracles used across goldenRenderSuite.cpp.
//
// These are pure CPU functions over captured RGBA8 frames - no Vulkan
// dependency - so they are unit-testable on their own (see
// goldenMetricsSuite.cpp) instead of only ever being exercised indirectly
// through a GPU golden test.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace Kataglyphis::Test::GoldenMetrics {

// A rectangular pixel region, half-open in both axes: [x0,x1) x [y0,y1).
struct Crop
{
    uint32_t x0;
    uint32_t x1;
    uint32_t y0;
    uint32_t y1;
};

// Excludes the ImGui overlay, which occupies the left ~72% of the frame plus
// a thin top/bottom margin. This is the crop every lit-vs-unlit / before-vs-
// after golden uses.
inline Crop panel_free_crop(uint32_t w, uint32_t h)
{
    return Crop{(w * 18U) / 25U, (w * 49U) / 50U, h / 20U, (h * 19U) / 20U};
}

// Narrower crop isolating the right-hand wall a second added model puts in
// frame, used by the texture-detail golden.
inline Crop card_crop(uint32_t w, uint32_t h)
{
    return Crop{(w * 37U) / 50U, (w * 49U) / 50U, h / 20U, (h * 19U) / 20U};
}

// Rec. 709 luma of one RGBA8 pixel, on a 0..255 scale.
inline double luminance_of(const std::vector<uint8_t> &rgba, size_t pixel)
{
    const size_t base = pixel * 4U;
    return 0.2126 * static_cast<double>(rgba[base]) + 0.7152 * static_cast<double>(rgba[base + 1U])
           + 0.0722 * static_cast<double>(rgba[base + 2U]);
}

// Mean Rec. 709 luminance of pixels within `crop`, on a 0..255 scale - the
// GUI-free counterpart of a whole-frame mean, for goldens that must compare
// brightness without the ImGui overlay drowning the signal (see
// panel_free_crop above).
inline double mean_luminance_in_crop(const std::vector<uint8_t> &rgba, uint32_t w, uint32_t h, Crop crop)
{
    double sum = 0.0;
    size_t count = 0;
    for (uint32_t y = crop.y0; y < crop.y1; ++y) {
        for (uint32_t x = crop.x0; x < crop.x1; ++x) {
            sum += luminance_of(rgba, static_cast<size_t>(y) * w + x);
            ++count;
        }
    }
    return count > 0U ? sum / static_cast<double>(count) : 0.0;
}

// Fraction of pixels within `crop` whose colour moves by more than 5 levels
// (on any channel) between captures `a` and `b`.
inline double swung_fraction(
  const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, uint32_t w, uint32_t h, Crop crop)
{
    size_t swung = 0;
    size_t total = 0;
    for (uint32_t y = crop.y0; y < crop.y1; ++y) {
        for (uint32_t x = crop.x0; x < crop.x1; ++x) {
            const size_t base = (static_cast<size_t>(y) * w + x) * 4U;
            for (size_t c = 0; c < 3U; ++c) {
                if (std::abs(static_cast<int>(a[base + c]) - static_cast<int>(b[base + c])) > 5) {
                    ++swung;
                    break;
                }
            }
            ++total;
        }
    }
    return total > 0U ? static_cast<double>(swung) / static_cast<double>(total) : 0.0;
}

// Fraction of pixels within `crop` whose right-hand neighbour differs in
// luminance by more than 6 levels - a texture-detail proxy that separates a
// real (sampled) texture or fine pattern from a flat fallback colour. The
// neighbour read stays inside `crop`: a crop of width 0 or 1 has no
// right-hand-neighbour pair to compare and returns 0.0, without callers
// having to shrink `x1` themselves first.
inline double detail_fraction(const std::vector<uint8_t> &rgba, uint32_t w, uint32_t h, Crop crop)
{
    if (crop.x1 == 0U || crop.x1 <= crop.x0 + 1U) { return 0.0; }
    size_t detailed = 0;
    size_t total = 0;
    for (uint32_t y = crop.y0; y < crop.y1; ++y) {
        for (uint32_t x = crop.x0; x < crop.x1 - 1U; ++x) {
            const size_t base = static_cast<size_t>(y) * w + x;
            if (std::abs(luminance_of(rgba, base) - luminance_of(rgba, base + 1U)) > 6.0) { ++detailed; }
            ++total;
        }
    }
    return total > 0U ? static_cast<double>(detailed) / static_cast<double>(total) : 0.0;
}

} // namespace Kataglyphis::Test::GoldenMetrics
