#pragma once
#include <vulkan/vulkan.h>

#include <vector>

#include "renderer/CommandBufferManager.hpp"

#include "vulkan_base/VulkanBuffer.hpp"

#include <cstring>

class VulkanBufferManager
{
  public:
    VulkanBufferManager();

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
      std::vector<T> &data);

    ~VulkanBufferManager();

  private:
    KataglyphisRenderer::VulkanRendererInternals::CommandBufferManager commandBufferManager;
};

template<typename T>
inline void VulkanBufferManager::createBufferAndUploadVectorOnDevice(VulkanDevice *device,
  VkCommandPool commandPool,
  VulkanBuffer &vulkanBuffer,
  VkBufferUsageFlags dstBufferUsageFlags,
  VkMemoryPropertyFlags dstBufferMemoryPropertyFlags,
  std::vector<T> &bufferData)
{
    VkDeviceSize bufferSize = sizeof(T) * bufferData.size();

    // temporary buffer to "stage" vertex data before transfering to GPU
    VulkanBuffer stagingBuffer;

    // create buffer and allocate memory to it
    stagingBuffer.create(device,
      bufferSize,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Map memory to vertex buffer
    // 1.) create pointer to a point in normal memory
    void *data;
    // 2.) map the vertex buffer memory to that point
    vkMapMemory(device->getLogicalDevice(), stagingBuffer.getBufferMemory(), 0, bufferSize, 0, &data);
    // 3.) copy memory from vertices vector to the point
    std::memcpy(data, bufferData.data(), static_cast<size_t>(bufferSize));
    // 4.) unmap the vertex buffer memory
    vkUnmapMemory(device->getLogicalDevice(), stagingBuffer.getBufferMemory());

    // create buffer with TRANSFER_DST_BIT to mark as recipient of transfer data
    // (also VERTEX_BUFFER) buffer memory is to be DEVICE_LOCAL_BIT meaning memory
    // is on the GPU and only accessible by it and not CPU (host)
    vulkanBuffer.create(device, bufferSize, dstBufferUsageFlags, dstBufferMemoryPropertyFlags);

    // copy staging buffer to vertex buffer on GPU
    copyBuffer(
      device->getLogicalDevice(), device->getGraphicsQueue(), commandPool, stagingBuffer, vulkanBuffer, bufferSize);

    stagingBuffer.cleanUp();
}
