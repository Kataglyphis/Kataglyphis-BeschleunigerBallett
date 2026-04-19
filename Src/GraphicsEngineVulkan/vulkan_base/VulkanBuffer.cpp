module;

#include <cstdint>
#include <utility>
#include <vulkan/vulkan.hpp>

#include "common/MemoryHelper.hpp"
#include "common/Utilities.hpp"
#include "spdlog/spdlog.h"

module kataglyphis.vulkan.buffer;

import kataglyphis.vulkan.device;

Kataglyphis::VulkanBuffer::VulkanBuffer() = default;

Kataglyphis::VulkanBuffer::VulkanBuffer(VulkanBuffer &&other) noexcept
  : device(other.device), buffer(other.buffer), bufferMemory(other.bufferMemory), created(other.created)
{
    other.device = nullptr;
    other.buffer = vk::Buffer{};
    other.bufferMemory = vk::DeviceMemory{};
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

        other.device = nullptr;
        other.buffer = vk::Buffer{};
        other.bufferMemory = vk::DeviceMemory{};
        other.created = false;
    }

    return *this;
}

void Kataglyphis::VulkanBuffer::create(VulkanDevice *vulkan_device,
  vk::DeviceSize buffer_size,
  vk::BufferUsageFlags buffer_usage_flags,
  vk::MemoryPropertyFlags buffer_propertiy_flags,
  vk::MemoryAllocateFlags buffer_allocate_flags)
{
    device = vulkan_device;

    if (buffer_size == 0) {
        buffer_size = 4; // Prevent VUID-VkBufferCreateInfo-size-00912
    }

    // information to create a buffer (doesn't include assigning memory)
    vk::BufferCreateInfo buffer_info{};
    buffer_info.size = buffer_size;
    // multiple types of buffer possible, e.g. vertex buffer
    buffer_info.usage = buffer_usage_flags;
    // similar to swap chain images, can share vertex buffers
    buffer_info.sharingMode = vk::SharingMode::eExclusive;

    buffer = device->getLogicalDevice().createBuffer(buffer_info).value;

    // get buffer memory requirements
    vk::MemoryRequirements memory_requirements = device->getLogicalDevice().getBufferMemoryRequirements(buffer);

    // allocate memory to buffer
    vk::MemoryAllocateInfo memory_alloc_info{};
    memory_alloc_info.allocationSize = memory_requirements.size;
    memory_alloc_info.pNext = nullptr;

    vk::MemoryAllocateFlagsInfo memory_allocate_flags_info{};
    if (buffer_allocate_flags) {
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
    bufferMemory = device->getLogicalDevice().allocateMemory(memory_alloc_info).value;

    // allocate memory to given buffer
    std::ignore = device->getLogicalDevice().bindBufferMemory(buffer, bufferMemory, 0);

    created = true;
}

void Kataglyphis::VulkanBuffer::cleanUp()
{
    if (created && device != nullptr) {
        device->getLogicalDevice().destroyBuffer(buffer);
        device->getLogicalDevice().freeMemory(bufferMemory);
    }

    buffer = vk::Buffer{};
    bufferMemory = vk::DeviceMemory{};
    created = false;
}

Kataglyphis::VulkanBuffer::~VulkanBuffer() = default;
