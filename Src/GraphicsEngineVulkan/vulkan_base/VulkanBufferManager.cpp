module;

#include "spdlog/spdlog.h"
#include <vulkan/vulkan_core.h>

module kataglyphis.vulkan.buffer_manager;

import kataglyphis.vulkan.command_buffer_manager;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.device;

namespace {
void copy_buffer_impl(VkDevice device,
  VkQueue transfer_queue,
  VkCommandPool transfer_command_pool,
  Kataglyphis::VulkanBuffer &src_buffer,
  Kataglyphis::VulkanBuffer &dst_buffer,
  VkDeviceSize buffer_size)
{
    VkCommandBuffer command_buffer =
      Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(device, transfer_command_pool);
    if (command_buffer == VK_NULL_HANDLE) {
        spdlog::error("Skipping buffer copy due to invalid command buffer.");
        return;
    }

    VkBufferCopy buffer_copy_region{};
    buffer_copy_region.srcOffset = 0;
    buffer_copy_region.dstOffset = 0;
    buffer_copy_region.size = buffer_size;

    vkCmdCopyBuffer(command_buffer, src_buffer.getBuffer(), dst_buffer.getBuffer(), 1, &buffer_copy_region);

    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(
      device, transfer_command_pool, transfer_queue, command_buffer);
}
}

Kataglyphis::VulkanBufferManager::VulkanBufferManager() = default;

void Kataglyphis::VulkanBufferManager::copyBuffer(VkDevice device,
  VkQueue transfer_queue,
  VkCommandPool transfer_command_pool,
  VulkanBuffer &src_buffer,
  VulkanBuffer &dst_buffer,
  VkDeviceSize buffer_size)
{
    copy_buffer_impl(device, transfer_queue, transfer_command_pool, src_buffer, dst_buffer, buffer_size);
}

void Kataglyphis::VulkanBufferManager::copyBuffer(VkDevice device,
  VkQueue transfer_queue,
  VkCommandPool transfer_command_pool,
  VulkanBuffer src_buffer,
  VulkanBuffer dst_buffer,
  VkDeviceSize buffer_size)
{
    copy_buffer_impl(device, transfer_queue, transfer_command_pool, src_buffer, dst_buffer, buffer_size);
}

void Kataglyphis::VulkanBufferManager::copyImageBuffer(VkDevice device,
  VkQueue transfer_queue,
  VkCommandPool transfer_command_pool,
  VkBuffer src_buffer,
  VkImage image,
  uint32_t width,
  uint32_t height)
{
    VkCommandBuffer transfer_command_buffer =
      Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(device, transfer_command_pool);
    if (transfer_command_buffer == VK_NULL_HANDLE) {
        spdlog::error("Skipping image buffer copy due to invalid command buffer.");
        return;
    }

    VkBufferImageCopy image_region{};
    image_region.bufferOffset = 0;
    image_region.bufferRowLength = 0;
    image_region.bufferImageHeight = 0;
    image_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_region.imageSubresource.mipLevel = 0;
    image_region.imageSubresource.baseArrayLayer = 0;
    image_region.imageSubresource.layerCount = 1;
    image_region.imageOffset = { .x = 0, .y = 0, .z = 0 };
    image_region.imageExtent = { .width = width, .height = height, .depth = 1 };

    vkCmdCopyBufferToImage(
      transfer_command_buffer, src_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_region);

    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(
      device, transfer_command_pool, transfer_queue, transfer_command_buffer);
}

Kataglyphis::VulkanBufferManager::~VulkanBufferManager() = default;
