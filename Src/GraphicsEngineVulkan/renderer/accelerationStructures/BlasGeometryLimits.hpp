#pragma once

#include <cstdint>

namespace Kataglyphis {
struct BlasTriangleLimits {
    uint32_t maxVertex;
    uint32_t primitiveCount;
};

// maxVertex is the highest addressable vertex index, not the vertex count -
// passing the count over-declares the buffer by one stride, and
// robustBufferAccess is off (VulkanDevice.cpp:484) so nothing clamps the
// read. The zero-vertex guard matters: without it, an empty mesh would wrap
// to 0xFFFFFFFF instead of staying 0.
constexpr BlasTriangleLimits blasTriangleLimits(uint32_t vertexCount, uint32_t indexCount)
{
    return { vertexCount == 0 ? 0 : vertexCount - 1, indexCount / 3 };
}
}// namespace Kataglyphis
