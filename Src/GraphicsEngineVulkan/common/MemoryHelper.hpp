#pragma once
#include <limits>
#include <vulkan/vulkan.hpp>

namespace Kataglyphis {
// aligned piece of memory appropiately and when necessary return bigger piece
[[maybe_unused]] static uint32_t align_up(uint32_t memory, uint32_t alignment)
{
    return (memory + alignment - 1) & ~(alignment - 1);
}

[[maybe_unused]] static uint32_t
  find_memory_type_index(vk::PhysicalDevice physical_device, uint32_t allowed_types, vk::MemoryPropertyFlags properties)
{
    // get properties of physical device memory
    vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
        if ((allowed_types & (1 << i))
            // index of memory type must match corresponding bit in allowedTypes
            // desired property bit flags are part of memory type's property flags
            && (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            // this memory type is valid, so return its index
            return i;
        }
    }

    return std::numeric_limits<uint32_t>::max();
}
}// namespace Kataglyphis