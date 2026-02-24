module;

#include "common/Utilities.hpp"
#include "renderer/VulkanRendererConfig.hpp"
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#ifndef VULKAN_API_VERSION
#define VULKAN_API_VERSION VK_API_VERSION_1_3
#endif

module kataglyphis.vulkan.allocator;

using namespace Kataglyphis;

Allocator::Allocator() = default;

Allocator::Allocator(const VkDevice &device, const VkPhysicalDevice &physicalDevice, const VkInstance &instance)
{
    // see here:
    // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html
    VmaAllocatorCreateInfo allocatorCreateInfo = {};
    allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorCreateInfo.vulkanApiVersion = VULKAN_API_VERSION;
    allocatorCreateInfo.physicalDevice = physicalDevice;
    allocatorCreateInfo.device = device;
    allocatorCreateInfo.instance = instance;

    ASSERT_VULKAN(vmaCreateAllocator(&allocatorCreateInfo, &vmaAllocator), "Failed to create vma allocator!")
}

void Allocator::cleanUp() { vmaDestroyAllocator(vmaAllocator); }

Allocator::~Allocator() = default;
