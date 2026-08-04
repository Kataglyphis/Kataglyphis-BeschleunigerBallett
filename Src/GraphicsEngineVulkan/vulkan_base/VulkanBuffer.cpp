module;
#include <memory>

#include <cstdint>
#include <tuple>
#include <utility>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

#include "common/Utilities.hpp"
#include "spdlog/spdlog.h"

module kataglyphis.vulkan.buffer;

import kataglyphis.vulkan.device;

Kataglyphis::VulkanBuffer::VulkanBuffer() = default;

Kataglyphis::VulkanBuffer::VulkanBuffer(VulkanBuffer &&other) noexcept
  : device(other.device), buffer(other.buffer), allocation(other.allocation), mappedData(other.mappedData),
    created(other.created)
{
    other.device = nullptr;
    other.buffer = vk::Buffer{};
    other.allocation = VK_NULL_HANDLE;
    other.mappedData = nullptr;
    other.created = false;
}

auto Kataglyphis::VulkanBuffer::operator=(VulkanBuffer &&other) noexcept -> VulkanBuffer &
{
    if (this != &other) {
        cleanUp();

        device = other.device;
        buffer = other.buffer;
        allocation = other.allocation;
        mappedData = other.mappedData;
        created = other.created;

        other.device = nullptr;
        other.buffer = vk::Buffer{};
        other.allocation = VK_NULL_HANDLE;
        other.mappedData = nullptr;
        other.created = false;
    }

    return *this;
}

void Kataglyphis::VulkanBuffer::create(const std::shared_ptr<VulkanDevice> &vulkan_device,
  vk::DeviceSize buffer_size,
  vk::BufferUsageFlags buffer_usage_flags,
  vk::MemoryPropertyFlags buffer_propertiy_flags,
  vk::MemoryAllocateFlags buffer_allocate_flags)
{
    cleanUp();

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

    // Map the requested vk::MemoryPropertyFlags onto a VMA allocation:
    // - requiredFlags guarantees an exact superset of the requested properties
    //   (e.g. HOST_VISIBLE | HOST_COHERENT or DEVICE_LOCAL).
    // - Host-visible buffers additionally get sequential-write host access and
    //   are persistently mapped, replacing the former mapMemory/unmapMemory
    //   pattern at the call sites.
    VmaAllocationCreateInfo allocation_create_info{};
    allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO;
    allocation_create_info.requiredFlags = static_cast<VkMemoryPropertyFlags>(buffer_propertiy_flags);
    if (buffer_propertiy_flags & vk::MemoryPropertyFlagBits::eHostVisible) {
        allocation_create_info.flags |=
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    // vk::MemoryAllocateFlagBits::eDeviceAddress is handled by VMA itself: the
    // allocator is created with VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT
    // and adds VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT to every allocation whose
    // buffer usage contains eShaderDeviceAddress. The parameter is kept for
    // source compatibility with existing callers.
    std::ignore = buffer_allocate_flags;

    const VkBufferCreateInfo &c_buffer_info = static_cast<const VkBufferCreateInfo &>(buffer_info);
    VkBuffer c_buffer = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info{};
    if (buffer_usage_flags & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
        // The device address of these buffers is consumed directly (SBT
        // regions, acceleration structure scratch); enforce the alignment the
        // implementation requires for such addresses.
        ASSERT_VULKAN(vmaCreateBufferWithAlignment(device->getVmaAllocator(),
                        &c_buffer_info,
                        &allocation_create_info,
                        static_cast<VkDeviceSize>(device->getMinDeviceAddressAlignment()),
                        &c_buffer,
                        &allocation,
                        &allocation_info),
          "Failed to create device-address buffer via VMA!");
    } else {
        ASSERT_VULKAN(vmaCreateBuffer(device->getVmaAllocator(),
                        &c_buffer_info,
                        &allocation_create_info,
                        &c_buffer,
                        &allocation,
                        &allocation_info),
          "Failed to create buffer via VMA!");
    }

    buffer = c_buffer;
    mappedData = allocation_info.pMappedData;

    created = true;
}

void Kataglyphis::VulkanBuffer::cleanUp()
{
    if (created && device != nullptr) {
        vmaDestroyBuffer(device->getVmaAllocator(), static_cast<VkBuffer>(buffer), allocation);
    }

    buffer = vk::Buffer{};
    allocation = VK_NULL_HANDLE;
    mappedData = nullptr;
    created = false;
}

Kataglyphis::VulkanBuffer::~VulkanBuffer() { cleanUp(); }
