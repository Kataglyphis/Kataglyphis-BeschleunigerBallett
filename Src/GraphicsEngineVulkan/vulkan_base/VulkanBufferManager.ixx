module;
#include <cstring>
#include <spdlog/spdlog.h>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.buffer_manager;

import kataglyphis.vulkan.command_buffer_manager;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.device;

export namespace Kataglyphis {
class VulkanBufferManager
{
  public:
    VulkanBufferManager();

    void copyBuffer(vk::Device device,
      vk::Queue transfer_queue,
      vk::CommandPool transfer_command_pool,
      VulkanBuffer &src_buffer,
      VulkanBuffer &dst_buffer,
      vk::DeviceSize buffer_size);

    void copyBuffer(vk::Device device,
      vk::Queue transfer_queue,
      vk::CommandPool transfer_command_pool,
      VulkanBuffer src_buffer,
      VulkanBuffer dst_buffer,
      vk::DeviceSize buffer_size);

    void copyImageBuffer(vk::Device device,
      vk::Queue transfer_queue,
      vk::CommandPool transfer_command_pool,
      vk::Buffer src_buffer,
      vk::Image image,
      uint32_t width,
      uint32_t height);

    template<typename T>
    void createBufferAndUploadVectorOnDevice(VulkanDevice *device,
      vk::CommandPool commandPool,
      VulkanBuffer &vulkanBuffer,
      vk::BufferUsageFlags dstBufferUsageFlags,
      vk::MemoryPropertyFlags dstBufferMemoryPropertyFlags,
      const std::vector<T> &data,
      vk::MemoryAllocateFlags dstBufferMemoryAllocateFlags = {},
      vk::Queue transfer_queue = {});

    template<typename T>
    void createBufferAndUploadVectorOnDevice(VulkanDevice *device,
      vk::CommandPool commandPool,
      VulkanBuffer &vulkanBuffer,
      vk::BufferUsageFlags dstBufferUsageFlags,
      vk::MemoryPropertyFlags dstBufferMemoryPropertyFlags,
      std::vector<T> &data,
      vk::MemoryAllocateFlags dstBufferMemoryAllocateFlags = {});

    ~VulkanBufferManager();

  private:
    Kataglyphis::VulkanRendererInternals::CommandBufferManager commandBufferManager;
};

template<typename T>
inline void VulkanBufferManager::createBufferAndUploadVectorOnDevice(VulkanDevice *device,
  vk::CommandPool commandPool,
  VulkanBuffer &vulkanBuffer,
  vk::BufferUsageFlags dstBufferUsageFlags,
  vk::MemoryPropertyFlags dstBufferMemoryPropertyFlags,
  const std::vector<T> &data,
  vk::MemoryAllocateFlags dstBufferMemoryAllocateFlags,
  vk::Queue transfer_queue)
{
    vk::DeviceSize bufferSize = sizeof(T) * data.size();
    if (bufferSize == 0) {
        bufferSize = sizeof(uint32_t);
        vulkanBuffer.create(
          device, bufferSize, dstBufferUsageFlags, dstBufferMemoryPropertyFlags, dstBufferMemoryAllocateFlags);
        return;
    }

    VulkanBuffer stagingBuffer;

    stagingBuffer.create(device,
      bufferSize,
      vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void *mapped_data = device->getLogicalDevice().mapMemory(stagingBuffer.getBufferMemory(), 0, bufferSize).value;
    std::memcpy(mapped_data, data.data(), static_cast<size_t>(bufferSize));
    device->getLogicalDevice().unmapMemory(stagingBuffer.getBufferMemory());

    vulkanBuffer.create(
      device, bufferSize, dstBufferUsageFlags, dstBufferMemoryPropertyFlags, dstBufferMemoryAllocateFlags);

    vk::Queue const queue = transfer_queue ? transfer_queue : device->getGraphicsQueue();
    auto const copy_buffer_ref = static_cast<void (VulkanBufferManager::*)(
      vk::Device, vk::Queue, vk::CommandPool, VulkanBuffer &, VulkanBuffer &, vk::DeviceSize)>(
      &VulkanBufferManager::copyBuffer);
    (this->*copy_buffer_ref)(device->getLogicalDevice(), queue, commandPool, stagingBuffer, vulkanBuffer, bufferSize);

    stagingBuffer.cleanUp();
}

template<typename T>
inline void VulkanBufferManager::createBufferAndUploadVectorOnDevice(VulkanDevice *device,
  vk::CommandPool commandPool,
  VulkanBuffer &vulkanBuffer,
  vk::BufferUsageFlags dstBufferUsageFlags,
  vk::MemoryPropertyFlags dstBufferMemoryPropertyFlags,
  std::vector<T> &data,
  vk::MemoryAllocateFlags dstBufferMemoryAllocateFlags)
{
    createBufferAndUploadVectorOnDevice(device,
      commandPool,
      vulkanBuffer,
      dstBufferUsageFlags,
      dstBufferMemoryPropertyFlags,
      static_cast<const std::vector<T> &>(data),
      dstBufferMemoryAllocateFlags,
      vk::Queue{});
}
}// namespace Kataglyphis
