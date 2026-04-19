module;

#include "spdlog/spdlog.h"
#include <vulkan/vulkan.hpp>

module kataglyphis.vulkan.buffer_manager;

import kataglyphis.vulkan.command_buffer_manager;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.device;

namespace {
void copy_buffer_impl(vk::Device device,
  vk::Queue transfer_queue,
  vk::CommandPool transfer_command_pool,
  Kataglyphis::VulkanBuffer &src_buffer,
  Kataglyphis::VulkanBuffer &dst_buffer,
  vk::DeviceSize buffer_size)
{
    vk::CommandBuffer command_buffer =
      Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(device, transfer_command_pool);
    if (!command_buffer) {
        spdlog::error("Skipping buffer copy due to invalid command buffer.");
        return;
    }

    vk::BufferCopy buffer_copy_region{};
    buffer_copy_region.srcOffset = 0;
    buffer_copy_region.dstOffset = 0;
    buffer_copy_region.size = buffer_size;

    command_buffer.copyBuffer(src_buffer.getBuffer(), dst_buffer.getBuffer(), buffer_copy_region);

    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(
      device, transfer_command_pool, transfer_queue, command_buffer);
}
}// namespace

Kataglyphis::VulkanBufferManager::VulkanBufferManager() = default;

void Kataglyphis::VulkanBufferManager::copyBuffer(vk::Device device,
  vk::Queue transfer_queue,
  vk::CommandPool transfer_command_pool,
  VulkanBuffer &src_buffer,
  VulkanBuffer &dst_buffer,
  vk::DeviceSize buffer_size)
{
    copy_buffer_impl(device, transfer_queue, transfer_command_pool, src_buffer, dst_buffer, buffer_size);
}



void Kataglyphis::VulkanBufferManager::copyImageBuffer(vk::Device device,
  vk::Queue transfer_queue,
  vk::CommandPool transfer_command_pool,
  vk::Buffer src_buffer,
  vk::Image image,
  uint32_t width,
  uint32_t height)
{
    vk::CommandBuffer transfer_command_buffer =
      Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(device, transfer_command_pool);
    if (!transfer_command_buffer) {
        spdlog::error("Skipping image buffer copy due to invalid command buffer.");
        return;
    }

    vk::BufferImageCopy image_region{};
    image_region.bufferOffset = 0;
    image_region.bufferRowLength = 0;
    image_region.bufferImageHeight = 0;
    image_region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    image_region.imageSubresource.mipLevel = 0;
    image_region.imageSubresource.baseArrayLayer = 0;
    image_region.imageSubresource.layerCount = 1;
    image_region.imageOffset = vk::Offset3D{ 0, 0, 0 };
    image_region.imageExtent = vk::Extent3D{ width, height, 1 };

    transfer_command_buffer.copyBufferToImage(src_buffer, image, vk::ImageLayout::eTransferDstOptimal, image_region);

    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(
      device, transfer_command_pool, transfer_queue, transfer_command_buffer);
}

Kataglyphis::VulkanBufferManager::~VulkanBufferManager() = default;
