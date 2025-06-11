#pragma once

#include <vulkan/vulkan.h>

#include "vulkan_base/VulkanBuffer.hpp"
namespace Kataglyphis::VulkanRendererInternals {
struct TopLevelAccelerationStructure
{
    VkAccelerationStructureKHR vulkanAS;
    VulkanBuffer vulkanBuffer;
};
}// namespace Kataglyphis::VulkanRendererInternals