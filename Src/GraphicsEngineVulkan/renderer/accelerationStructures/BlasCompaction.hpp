#pragma once

#include <cstdint>
#include <span>
#include <vulkan/vulkan.hpp>

namespace Kataglyphis {

// VkBufferCreateInfo::size must be greater than zero
// (VUID-VkBufferCreateInfo-size-00912), so a driver reporting an empty
// result set, or a 0-byte compacted size for any entry, cannot be honoured -
// the caller must keep the uncompacted BLAS instead.
[[nodiscard]] inline auto compactedSizesAreUsable(std::span<const vk::DeviceSize> sizes) -> bool
{
    if (sizes.empty()) { return false; }
    for (vk::DeviceSize const size : sizes) {
        if (size == 0) { return false; }
    }
    return true;
}

}// namespace Kataglyphis
