#include "vulkan_base/VulkanBufferManager.hpp"
#include "common/Utilities.hpp"

Kataglyphis::VulkanBufferManager::VulkanBufferManager() {}

void Kataglyphis::VulkanBufferManager::copyBuffer(VkDevice device,
  VkQueue transfer_queue,
  VkCommandPool transfer_command_pool,
  VulkanBuffer src_buffer,
  VulkanBuffer dst_buffer,
  VkDeviceSize buffer_size)
{
    // create buffer
    VkCommandBuffer command_buffer = commandBufferManager.beginCommandBuffer(device, transfer_command_pool);

    // region of data to copy from and to
    VkBufferCopy buffer_copy_region{};
    buffer_copy_region.srcOffset = 0;
    buffer_copy_region.dstOffset = 0;
    buffer_copy_region.size = buffer_size;

    // command to copy src buffer to dst buffer
    vkCmdCopyBuffer(command_buffer, src_buffer.getBuffer(), dst_buffer.getBuffer(), 1, &buffer_copy_region);

    commandBufferManager.endAndSubmitCommandBuffer(device, transfer_command_pool, transfer_queue, command_buffer);
}

void Kataglyphis::VulkanBufferManager::copyImageBuffer(VkDevice device,
  VkQueue transfer_queue,
  VkCommandPool transfer_command_pool,
  VkBuffer src_buffer,
  VkImage image,
  uint32_t width,
  uint32_t height)
{
    // create buffer
    VkCommandBuffer transfer_command_buffer = commandBufferManager.beginCommandBuffer(device, transfer_command_pool);

    VkBufferImageCopy image_region{};
    image_region.bufferOffset = 0;// offset into data
    image_region.bufferRowLength = 0;// row length of data to calculate data spacing
    image_region.bufferImageHeight = 0;// image height to calculate data spacing
    image_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;// which aspect of image to copy
    image_region.imageSubresource.mipLevel = 0;
    image_region.imageSubresource.baseArrayLayer = 0;
    image_region.imageSubresource.layerCount = 1;
    image_region.imageOffset = { 0, 0, 0 };// offset into image
    image_region.imageExtent = { width, height, 1 };

    // copy buffer to given image
    vkCmdCopyBufferToImage(
      transfer_command_buffer, src_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_region);

    commandBufferManager.endAndSubmitCommandBuffer(
      device, transfer_command_pool, transfer_queue, transfer_command_buffer);
}

Kataglyphis::VulkanBufferManager::~VulkanBufferManager() {}
