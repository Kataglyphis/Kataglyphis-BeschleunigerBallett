#pragma once

#include <cstdint>
#include <span>
#include <vulkan/vulkan.hpp>

#include "shared/scene/ObjMaterial.hpp"

namespace Kataglyphis {
struct BlasTriangleLimits {
    uint32_t maxVertex;
    uint32_t primitiveCount;
};

// maxVertex is the highest addressable vertex index, not the vertex count -
// passing the count over-declares the buffer by one stride, and
// robustBufferAccess is off (VulkanDevice::create_logical_device sets
// robustBufferAccess = VK_FALSE) so nothing clamps the read. The zero-vertex
// guard matters: without it, an empty mesh would wrap to 0xFFFFFFFF instead
// of staying 0.
constexpr BlasTriangleLimits blasTriangleLimits(uint32_t vertexCount, uint32_t indexCount)
{
    return { vertexCount == 0 ? 0 : vertexCount - 1, indexCount / 3 };
}

// A mesh needs any-hit invocation (i.e. must not be eOpaque) iff at least one
// of its materials is a glTF MASK material - see ObjMaterial.hpp's
// alphaCutoff convention: alphaCutoff >= 0 means MASK, negative means
// OPAQUE/BLEND. A single MASK material among many OPAQUE ones is enough,
// since all of a mesh's geometry shares one BLAS geometry entry and one flag.
constexpr bool blasGeometryNeedsAnyHit(std::span<const ObjMaterial> materials)
{
    for (const ObjMaterial &material : materials) {
        if (material.alphaCutoff >= 0.0F) { return true; }
    }
    return false;
}

// eOpaque suppresses any-hit shader invocation entirely, which is the fast
// path for geometry that has no MASK material to alpha-test. Geometry that
// does must stay non-opaque so raytrace.rahit.slang's alpha test still runs.
constexpr vk::GeometryFlagsKHR blasGeometryFlags(bool needsAnyHit)
{
    return needsAnyHit ? vk::GeometryFlagsKHR{} : vk::GeometryFlagsKHR{ vk::GeometryFlagBitsKHR::eOpaque };
}
}// namespace Kataglyphis
