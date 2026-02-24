#pragma once

#include <vulkan/vulkan.h>

import kataglyphis.vulkan.buffer;
namespace Kataglyphis::VulkanRendererInternals {
struct TopLevelAccelerationStructure
{
    VkAccelerationStructureKHR vulkanAS;
    VulkanBuffer vulkanBuffer;
};
}// namespace Kataglyphis::VulkanRendererInternals