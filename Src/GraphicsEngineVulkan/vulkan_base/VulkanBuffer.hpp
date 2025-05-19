#pragma once
#include <vulkan/vulkan.h>

#include "vulkan_base/VulkanDevice.hpp"

class VulkanBuffer
{
  public:
    VulkanBuffer();

    void create(VulkanDevice *vulkanDevice,
      VkDeviceSize buffer_size,
      VkBufferUsageFlags buffer_usage_flags,
      VkMemoryPropertyFlags buffer_propertiy_flags);

    void cleanUp();

    VkBuffer &getBuffer() { return buffer; };
    VkDeviceMemory &getBufferMemory() { return bufferMemory; };

    ~VulkanBuffer();

  private:
    VulkanDevice *device{ VK_NULL_HANDLE };

    VkBuffer buffer{ VK_NULL_HANDLE };
    VkDeviceMemory bufferMemory{ VK_NULL_HANDLE };

    bool created{ false };
};
