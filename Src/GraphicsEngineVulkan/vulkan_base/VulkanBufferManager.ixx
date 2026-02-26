module;
#include <cstring>
#include <spdlog/spdlog.h>
#include <vector>
#include <vulkan/vulkan.h>

export module kataglyphis.vulkan.buffer_manager;

import kataglyphis.vulkan.command_buffer_manager;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.device;

export namespace Kataglyphis {
class VulkanBufferManager
{
  public:
    VulkanBufferManager();

    void copyBuffer(VkDevice device,
      VkQueue transfer_queue,
      VkCommandPool transfer_command_pool,
      VulkanBuffer &src_buffer,
      VulkanBuffer &dst_buffer,
      VkDeviceSize buffer_size);

    void copyBuffer(VkDevice device,
      VkQueue transfer_queue,
      VkCommandPool transfer_command_pool,
      VulkanBuffer src_buffer,
      VulkanBuffer dst_buffer,
      VkDeviceSize buffer_size);

    void copyImageBuffer(VkDevice device,
      VkQueue transfer_queue,
      VkCommandPool transfer_command_pool,
      VkBuffer src_buffer,
      VkImage image,
      uint32_t width,
      uint32_t height);

    template<typename T>
    void createBufferAndUploadVectorOnDevice(VulkanDevice *device,
      VkCommandPool commandPool,
      VulkanBuffer &vulkanBuffer,
      VkBufferUsageFlags dstBufferUsageFlags,
      VkMemoryPropertyFlags dstBufferMemoryPropertyFlags,
      const std::vector<T> &data,
      VkMemoryAllocateFlags dstBufferMemoryAllocateFlags = 0,
      VkQueue transfer_queue = VK_NULL_HANDLE);

    template<typename T>
    void createBufferAndUploadVectorOnDevice(VulkanDevice *device,
      VkCommandPool commandPool,
      VulkanBuffer &vulkanBuffer,
      VkBufferUsageFlags dstBufferUsageFlags,
      VkMemoryPropertyFlags dstBufferMemoryPropertyFlags,
      std::vector<T> &data,
      VkMemoryAllocateFlags dstBufferMemoryAllocateFlags = 0);

    ~VulkanBufferManager();

  private:
    Kataglyphis::VulkanRendererInternals::CommandBufferManager commandBufferManager;
};

template<typename T>
inline void VulkanBufferManager::createBufferAndUploadVectorOnDevice(VulkanDevice *device,
  VkCommandPool commandPool,
  VulkanBuffer &vulkanBuffer,
  VkBufferUsageFlags dstBufferUsageFlags,
  VkMemoryPropertyFlags dstBufferMemoryPropertyFlags,
  const std::vector<T> &data,
  VkMemoryAllocateFlags dstBufferMemoryAllocateFlags,
  VkQueue transfer_queue)
{
    VkDeviceSize bufferSize = sizeof(T) * data.size();
    if (bufferSize == 0) {
        bufferSize = sizeof(uint32_t);
        vulkanBuffer.create(
          device, bufferSize, dstBufferUsageFlags, dstBufferMemoryPropertyFlags, dstBufferMemoryAllocateFlags);
        return;
    }

    VulkanBuffer stagingBuffer;

    stagingBuffer.create(device,
      bufferSize,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void *mapped_data;
    vkMapMemory(device->getLogicalDevice(), stagingBuffer.getBufferMemory(), 0, bufferSize, 0, &mapped_data);
    std::memcpy(mapped_data, data.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(device->getLogicalDevice(), stagingBuffer.getBufferMemory());

    vulkanBuffer.create(
      device, bufferSize, dstBufferUsageFlags, dstBufferMemoryPropertyFlags, dstBufferMemoryAllocateFlags);

    VkQueue const queue = (transfer_queue != VK_NULL_HANDLE) ? transfer_queue : device->getGraphicsQueue();
    auto const copy_buffer_ref = static_cast<void (VulkanBufferManager::*)(VkDevice,
      VkQueue,
      VkCommandPool,
      VulkanBuffer &,
      VulkanBuffer &,
      VkDeviceSize)>(&VulkanBufferManager::copyBuffer);
    (this->*copy_buffer_ref)(device->getLogicalDevice(), queue, commandPool, stagingBuffer, vulkanBuffer, bufferSize);

    stagingBuffer.cleanUp();
}

template<typename T>
inline void VulkanBufferManager::createBufferAndUploadVectorOnDevice(VulkanDevice *device,
  VkCommandPool commandPool,
  VulkanBuffer &vulkanBuffer,
  VkBufferUsageFlags dstBufferUsageFlags,
  VkMemoryPropertyFlags dstBufferMemoryPropertyFlags,
  std::vector<T> &data,
  VkMemoryAllocateFlags dstBufferMemoryAllocateFlags)
{
    createBufferAndUploadVectorOnDevice(device,
      commandPool,
      vulkanBuffer,
      dstBufferUsageFlags,
      dstBufferMemoryPropertyFlags,
      static_cast<const std::vector<T> &>(data),
      dstBufferMemoryAllocateFlags,
      VK_NULL_HANDLE);
}
}// namespace Kataglyphis
