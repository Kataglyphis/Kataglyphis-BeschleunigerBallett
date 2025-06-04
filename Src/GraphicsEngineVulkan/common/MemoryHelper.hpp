#pragma once
#include <vulkan/vulkan.h>

namespace Kataglyphis {
// aligned piece of memory appropiately and when necessary return bigger piece
static uint32_t align_up(uint32_t memory, uint32_t alignment) { return (memory + alignment - 1) & ~(alignment - 1); }

static uint32_t
  find_memory_type_index(VkPhysicalDevice physical_device, uint32_t allowed_types, VkMemoryPropertyFlags properties)
{
    // get properties of physical device memory
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
        if ((allowed_types & (1 << i))
            // index of memory type must match corresponding bit in allowedTypes
            // desired property bit flags are part of memory type's property flags
            && (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            // this memory type is valid, so return its index
            return i;
        }
    }

    return -1;
}
}// namespace Kataglyphis