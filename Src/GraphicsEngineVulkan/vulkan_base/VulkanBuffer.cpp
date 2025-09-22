#include "vulkan_base/VulkanBuffer.hpp"

#include <stdexcept>

#include "common/MemoryHelper.hpp"
#include "common/Utilities.hpp"

Kataglyphis::VulkanBuffer::VulkanBuffer() {}

void Kataglyphis::VulkanBuffer::create(VulkanDevice *device,
  VkDeviceSize buffer_size,
  VkBufferUsageFlags buffer_usage_flags,
  VkMemoryPropertyFlags buffer_propertiy_flags)
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

    uint32_t memory_type_index = Kataglyphis::find_memory_type_index(
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
    if (created) {
        vkDestroyBuffer(device->getLogicalDevice(), buffer, nullptr);
        vkFreeMemory(device->getLogicalDevice(), bufferMemory, nullptr);
    }
}

Kataglyphis::VulkanBuffer::~VulkanBuffer() {}
