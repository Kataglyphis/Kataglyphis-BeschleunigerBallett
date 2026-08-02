#pragma once

#include <cstddef>
#include <span>

namespace Kataglyphis {

// Returns true iff every face matches the first face's dimensions and none
// are degenerate. The cubemap upload copies `layerSize` bytes out of every
// face, so a face larger than the first reads off the end of an earlier
// allocation and a smaller one writes overlapping layers.
constexpr bool cubemapFacesConsistent(std::span<const int, 6> widths, std::span<const int, 6> heights)
{
    for (size_t i = 0; i < 6; ++i) {
        if (widths[i] <= 0 || heights[i] <= 0) { return false; }
        if (widths[i] != widths[0] || heights[i] != heights[0]) { return false; }
    }
    return true;
}

// The fallback face SkyBox uploads when a real face fails to load or the six
// faces disagree in size: one opaque-black RGBA8 texel. Shared with the test
// suite so the shipped byte pattern cannot drift from what is asserted here.
inline constexpr unsigned char kFallbackCubemapFacePixel[4] = { 0, 0, 0, 255 };

}// namespace Kataglyphis
