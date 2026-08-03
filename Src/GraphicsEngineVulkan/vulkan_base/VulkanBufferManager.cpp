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

    // Record the copy through the command-buffer overload so the region setup
    // lives in exactly one place.
    copyImageBuffer(transfer_command_buffer, src_buffer, image, width, height);

    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(
      device, transfer_command_pool, transfer_queue, transfer_command_buffer);
}

void Kataglyphis::VulkanBufferManager::copyImageBuffer(vk::CommandBuffer command_buffer,
  vk::Buffer src_buffer,
  vk::Image image,
  uint32_t width,
  uint32_t height)
{
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

    command_buffer.copyBufferToImage(src_buffer, image, vk::ImageLayout::eTransferDstOptimal, image_region);
}

void Kataglyphis::VulkanBufferManager::ensureStagingBufferCapacity(const std::shared_ptr<VulkanDevice> &device,
  vk::DeviceSize size)
{
    if (stagingBuffer.getMappedData() != nullptr && stagingBufferCapacity >= size) { return; }

    // Grow geometrically so repeated uploads converge to zero reallocations.
    constexpr vk::DeviceSize initial_capacity = 64ULL * 1024ULL;
    vk::DeviceSize new_capacity = stagingBufferCapacity > initial_capacity ? stagingBufferCapacity : initial_capacity;
    while (new_capacity < size) { new_capacity *= 2; }

    // Safe to destroy here: every upload submit is fence-synchronized before
    // returning, so no previously recorded copy can still reference it.
    // Redundant since create() now releases the previous allocation itself,
    // but kept for that synchronisation argument.
    stagingBuffer.cleanUp();
    stagingBuffer.create(device,
      new_capacity,
      vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    stagingBufferCapacity = new_capacity;
}

void Kataglyphis::VulkanBufferManager::cleanUp()
{
    stagingBuffer.cleanUp();
    stagingBufferCapacity = 0;
}

Kataglyphis::VulkanBufferManager::~VulkanBufferManager() = default;
