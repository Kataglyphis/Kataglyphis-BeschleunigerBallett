module;
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.buffer;

import kataglyphis.vulkan.device;

export namespace Kataglyphis {
class VulkanBuffer
{
  public:
    VulkanBuffer();
    VulkanBuffer(const VulkanBuffer &) = delete;
    VulkanBuffer &operator=(const VulkanBuffer &) = delete;
    VulkanBuffer(VulkanBuffer &&other) noexcept;
    VulkanBuffer &operator=(VulkanBuffer &&other) noexcept;

    void create(VulkanDevice *vulkanDevice,
      vk::DeviceSize buffer_size,
      vk::BufferUsageFlags buffer_usage_flags,
      vk::MemoryPropertyFlags buffer_propertiy_flags,
      vk::MemoryAllocateFlags buffer_allocate_flags = {});

    void cleanUp();

    vk::Buffer &getBuffer() { return buffer; };
    vk::DeviceMemory &getBufferMemory() { return bufferMemory; };

    ~VulkanBuffer();

  private:
    VulkanDevice *device{ nullptr };

    vk::Buffer buffer{};
    vk::DeviceMemory bufferMemory{};

    bool created{ false };
};
}// namespace Kataglyphis
