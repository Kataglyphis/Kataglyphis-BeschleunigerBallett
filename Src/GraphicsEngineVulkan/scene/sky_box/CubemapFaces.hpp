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

}// namespace Kataglyphis
