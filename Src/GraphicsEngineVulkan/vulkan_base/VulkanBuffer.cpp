module;

#include <cstdint>
#include <utility>
#include <vulkan/vulkan_core.h>

#include "common/MemoryHelper.hpp"
#include "common/Utilities.hpp"
#include "spdlog/spdlog.h"

module kataglyphis.vulkan.buffer;

import kataglyphis.vulkan.device;

Kataglyphis::VulkanBuffer::VulkanBuffer() = default;

Kataglyphis::VulkanBuffer::VulkanBuffer(VulkanBuffer &&other) noexcept
  : device(other.device), buffer(other.buffer), bufferMemory(other.bufferMemory), created(other.created)
{
    other.device = VK_NULL_HANDLE;
    other.buffer = VK_NULL_HANDLE;
    other.bufferMemory = VK_NULL_HANDLE;
    other.created = false;
}

auto Kataglyphis::VulkanBuffer::operator=(VulkanBuffer &&other) noexcept -> VulkanBuffer &
{
    if (this != &other) {
        cleanUp();

        device = other.device;
        buffer = other.buffer;
        bufferMemory = other.bufferMemory;
        created = other.created;

        other.device = VK_NULL_HANDLE;
        other.buffer = VK_NULL_HANDLE;
        other.bufferMemory = VK_NULL_HANDLE;
        other.created = false;
    }

    return *this;
}

void Kataglyphis::VulkanBuffer::create(VulkanDevice *device,
  VkDeviceSize buffer_size,
  VkBufferUsageFlags buffer_usage_flags,
  VkMemoryPropertyFlags buffer_propertiy_flags,
  VkMemoryAllocateFlags buffer_allocate_flags)
{
    this->device = device;

    // information to create a buffer (doesn't include assigning memory)
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    // multiple types of buffer possible, e.g. vertex buffer
    buffer_info.usage = buffer_usage_flags;
    // similar to swap chain images, can share vertex buffers
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(device->getLogicalDevice(), &buffer_info, nullptr, &buffer);
    ASSERT_VULKAN(result, "Failed to create a buffer!");

    // get buffer memory requirements
    VkMemoryRequirements memory_requirements{};
    vkGetBufferMemoryRequirements(device->getLogicalDevice(), buffer, &memory_requirements);

    // allocate memory to buffer
    VkMemoryAllocateInfo memory_alloc_info{};
    memory_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory_alloc_info.allocationSize = memory_requirements.size;
    memory_alloc_info.pNext = nullptr;

    VkMemoryAllocateFlagsInfo memory_allocate_flags_info{};
    if (buffer_allocate_flags != 0) {
        memory_allocate_flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        memory_allocate_flags_info.flags = buffer_allocate_flags;
        memory_alloc_info.pNext = &memory_allocate_flags_info;
    }

    uint32_t const memory_type_index = Kataglyphis::find_memory_type_index(
      device->getPhysicalDevice(), memory_requirements.memoryTypeBits, buffer_propertiy_flags);

    // VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |		/* memory is visible to
    // CPU side
    // */ VK_MEMORY_PROPERTY_HOST_COHERENT_BIT	/* data is placed straight into
    // buffer */);
    if (memory_type_index < 0) { spdlog::error("Failed to find suitable memory type!"); }

    memory_alloc_info.memoryTypeIndex = memory_type_index;

    // allocate memory to VkDeviceMemory
    result = vkAllocateMemory(device->getLogicalDevice(), &memory_alloc_info, nullptr, &bufferMemory);
    ASSERT_VULKAN(result, "Failed to allocate memory for buffer!");

    // allocate memory to given buffer
    vkBindBufferMemory(device->getLogicalDevice(), buffer, bufferMemory, 0);

    created = true;
}

void Kataglyphis::VulkanBuffer::cleanUp()
{
    if (created && device != VK_NULL_HANDLE) {
        vkDestroyBuffer(device->getLogicalDevice(), buffer, nullptr);
        vkFreeMemory(device->getLogicalDevice(), bufferMemory, nullptr);
    }

    buffer = VK_NULL_HANDLE;
    bufferMemory = VK_NULL_HANDLE;
    created = false;
}

Kataglyphis::VulkanBuffer::~VulkanBuffer() = default;
