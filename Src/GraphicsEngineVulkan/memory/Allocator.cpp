module;

#include "common/Utilities.hpp"
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

#ifndef VULKAN_API_VERSION
#define VULKAN_API_VERSION VK_API_VERSION_1_3
#endif

module kataglyphis.vulkan.allocator;

import kataglyphis.vulkan.config;

using namespace Kataglyphis;

Allocator::Allocator() = default;

Allocator::Allocator(const vk::Device &device, const vk::PhysicalDevice &physicalDevice, const vk::Instance &instance)
{
    VmaAllocatorCreateInfo allocatorCreateInfo = {};
    allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorCreateInfo.vulkanApiVersion = Kataglyphis::RendererConfig::vulkanApiVersion;
    allocatorCreateInfo.physicalDevice = static_cast<VkPhysicalDevice>(physicalDevice);
    allocatorCreateInfo.device = static_cast<VkDevice>(device);
    allocatorCreateInfo.instance = static_cast<VkInstance>(instance);

    ASSERT_VULKAN(vmaCreateAllocator(&allocatorCreateInfo, &vmaAllocator), "Failed to create vma allocator!")
}

void Allocator::cleanUp() { vmaDestroyAllocator(vmaAllocator); }

Allocator::~Allocator() = default;