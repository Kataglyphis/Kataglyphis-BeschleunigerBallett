#pragma once

#include <vulkan/vulkan.h>

#include "vulkan_base/VulkanBuffer.hpp"

struct TopLevelAccelerationStructure
{
    VkAccelerationStructureKHR vulkanAS;
    VulkanBuffer vulkanBuffer;
};
