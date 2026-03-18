#pragma once
#include <vulkan/vulkan.hpp>

import kataglyphis.vulkan.buffer;

namespace Kataglyphis::VulkanRendererInternals {
struct BottomLevelAccelerationStructure
{
    vk::AccelerationStructureKHR vulkanAS;
    VulkanBuffer vulkanBuffer;
};
}// namespace Kataglyphis::VulkanRendererInternals