#include "memory/Allocator.hpp"

#include "common/Utilities.hpp"

using namespace Kataglyphis;

Allocator::Allocator() {}

Allocator::Allocator(const VkDevice &device, const VkPhysicalDevice &physicalDevice, const VkInstance &instance)
{
    // see here:
    // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html
    VmaAllocatorCreateInfo allocatorCreateInfo = {};
    allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorCreateInfo.physicalDevice = physicalDevice;
    allocatorCreateInfo.device = device;
    allocatorCreateInfo.instance = instance;

    ASSERT_VULKAN(vmaCreateAllocator(&allocatorCreateInfo, &vmaAllocator), "Failed to create vma allocator!")
}

void Allocator::cleanUp() { vmaDestroyAllocator(vmaAllocator); }

Allocator::~Allocator() {}
