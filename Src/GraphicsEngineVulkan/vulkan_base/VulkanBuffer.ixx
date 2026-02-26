module;
#include <vulkan/vulkan.h>

export module kataglyphis.vulkan.buffer;

import kataglyphis.vulkan.device;

export namespace Kataglyphis {
class VulkanBuffer
{
  public:
    VulkanBuffer();
    VulkanBuffer(const VulkanBuffer &) = delete;
    auto operator=(const VulkanBuffer &) -> VulkanBuffer & = delete;
    VulkanBuffer(VulkanBuffer &&other) noexcept;
    auto operator=(VulkanBuffer &&other) noexcept -> VulkanBuffer &;

    void create(VulkanDevice *vulkanDevice,
      VkDeviceSize buffer_size,
      VkBufferUsageFlags buffer_usage_flags,
      VkMemoryPropertyFlags buffer_propertiy_flags,
      VkMemoryAllocateFlags buffer_allocate_flags = 0);

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
}// namespace Kataglyphis
