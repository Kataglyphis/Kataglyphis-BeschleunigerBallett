#pragma once
#include <vulkan/vulkan.h>

#include "vulkan_base/VulkanBuffer.hpp"

struct BottomLevelAccelerationStructure
{
    VkAccelerationStructureKHR vulkanAS;
    VulkanBuffer vulkanBuffer;
};
