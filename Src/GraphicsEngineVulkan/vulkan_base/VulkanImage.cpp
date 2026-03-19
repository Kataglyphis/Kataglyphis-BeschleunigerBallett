module;

#include <cstdint>
#include <tuple>
#include <utility>
#include <vulkan/vulkan.hpp>

#include "common/MemoryHelper.hpp"
#include "common/Utilities.hpp"

module kataglyphis.vulkan.image;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.command_buffer_manager;

Kataglyphis::VulkanImage::VulkanImage() = default;

Kataglyphis::VulkanImage::VulkanImage(VulkanImage &&other) noexcept
  : device(other.device), image(other.image), imageMemory(other.imageMemory)
{
    other.device = nullptr;
    other.image = nullptr;
    other.imageMemory = nullptr;
}

auto Kataglyphis::VulkanImage::operator=(VulkanImage &&other) noexcept -> VulkanImage &
{
    if (this != &other) {
        cleanUp();

        device = other.device;
        image = other.image;
        imageMemory = other.imageMemory;

        other.device = nullptr;
        other.image = nullptr;
        other.imageMemory = nullptr;
    }

    return *this;
}

void Kataglyphis::VulkanImage::create(VulkanDevice *in_device,
  uint32_t width,
  uint32_t height,
  uint32_t mip_levels,
  vk::Format format,
  vk::ImageTiling tiling,
  vk::ImageUsageFlags use_flags,
  vk::MemoryPropertyFlags prop_flags)
{
    this->device = in_device;
    // CREATE image
    // image creation info
    vk::ImageCreateInfo image_create_info{};
    image_create_info.imageType = vk::ImageType::e2D;// type of image (1D, 2D, 3D)
    image_create_info.extent.width = width;// width if image extent
    image_create_info.extent.height = height;// height if image extent
    image_create_info.extent.depth = 1;// height if image extent
    image_create_info.mipLevels = mip_levels;// number of mipmap levels
    image_create_info.arrayLayers = 1;// number of levels in image array
    image_create_info.format = format;// format type of image
    image_create_info.tiling = tiling;// tiling of image ("arranged" for optimal reading)
    image_create_info.initialLayout = vk::ImageLayout::eUndefined;// layout of image data on creation
    image_create_info.usage = use_flags;// bit flags defining what image will be used for
    image_create_info.samples = vk::SampleCountFlagBits::e1;// number of samples for multisampling
    image_create_info.sharingMode = vk::SharingMode::eExclusive;// whether image can be shared between queues

    image = device->getLogicalDevice().createImage(image_create_info).value;

    // CREATE memory for image
    // get memory requirements for a type of image
    vk::MemoryRequirements memory_requirements = device->getLogicalDevice().getImageMemoryRequirements(image);

    // allocate memory using image requirements and user defined properties
    vk::MemoryAllocateInfo memory_alloc_info{};
    memory_alloc_info.allocationSize = memory_requirements.size;
    memory_alloc_info.memoryTypeIndex =
      Kataglyphis::find_memory_type_index(device->getPhysicalDevice(), memory_requirements.memoryTypeBits, prop_flags);

    imageMemory = device->getLogicalDevice().allocateMemory(memory_alloc_info).value;

    // connect memory to image
    std::ignore = device->getLogicalDevice().bindImageMemory(image, imageMemory, 0);
}

void Kataglyphis::VulkanImage::transitionImageLayout(vk::Device in_logical_device,
  vk::Queue queue,
  vk::CommandPool command_pool,
  vk::ImageLayout old_layout,
  vk::ImageLayout new_layout,
  vk::ImageAspectFlags aspectMask,
  uint32_t mip_levels)
{
    vk::CommandBuffer command_buffer =
      Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(in_logical_device, command_pool);

    // vk::ImageAspectFlagBits::eColor
    vk::ImageMemoryBarrier memory_barrier{};
    memory_barrier.oldLayout = old_layout;
    memory_barrier.newLayout = new_layout;
    memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;// Queue family to transition from
    memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;// Queue family to transition to
    memory_barrier.image = image;// image being accessed and modified as part of barrier
    memory_barrier.subresourceRange.aspectMask = aspectMask;// aspect of image being altered
    memory_barrier.subresourceRange.baseMipLevel = 0;// first mip level to start alterations on
    memory_barrier.subresourceRange.levelCount = mip_levels;// number of mip levels to alter starting from baseMipLevel
    memory_barrier.subresourceRange.baseArrayLayer = 0;// first layer to start alterations on
    memory_barrier.subresourceRange.layerCount = 1;// number of layers to alter starting from baseArrayLayer

    // if transitioning from new image to image ready to receive data
    memory_barrier.srcAccessMask = accessFlagsForImageLayout(old_layout);
    memory_barrier.dstAccessMask = accessFlagsForImageLayout(new_layout);

    vk::PipelineStageFlags const src_stage = pipelineStageForLayout(old_layout);
    vk::PipelineStageFlags const dst_stage = pipelineStageForLayout(new_layout);

    command_buffer.pipelineBarrier(src_stage,
      dst_stage,// pipeline stages (match to src and dst accessmask)
      {},// no dependency flags
      nullptr,// memory barriers
      nullptr,// buffer memory barriers
      memory_barrier// image memory barriers
    );

    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(
      in_logical_device, command_pool, queue, command_buffer);
}

void Kataglyphis::VulkanImage::transitionImageLayout(vk::CommandBuffer command_buffer,
  vk::ImageLayout old_layout,
  vk::ImageLayout new_layout,
  uint32_t mip_levels,
  vk::ImageAspectFlags aspectMask)
{
    vk::ImageMemoryBarrier memory_barrier{};
    memory_barrier.oldLayout = old_layout;
    memory_barrier.newLayout = new_layout;
    memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;// Queue family to transition from
    memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;// Queue family to transition to
    memory_barrier.image = image;// image being accessed and modified as part of barrier
    memory_barrier.subresourceRange.aspectMask = aspectMask;// aspect of image being altered
    memory_barrier.subresourceRange.baseMipLevel = 0;// first mip level to start alterations on
    memory_barrier.subresourceRange.levelCount = mip_levels;// number of mip levels to alter starting from baseMipLevel
    memory_barrier.subresourceRange.baseArrayLayer = 0;// first layer to start alterations on
    memory_barrier.subresourceRange.layerCount = 1;// number of layers to alter starting from baseArrayLayer

    memory_barrier.srcAccessMask = accessFlagsForImageLayout(old_layout);
    memory_barrier.dstAccessMask = accessFlagsForImageLayout(new_layout);

    vk::PipelineStageFlags const src_stage = pipelineStageForLayout(old_layout);
    vk::PipelineStageFlags const dst_stage = pipelineStageForLayout(new_layout);

    // if transitioning from new image to image ready to receive data

    command_buffer.pipelineBarrier(src_stage,
      dst_stage,// pipeline stages (match to src and dst accessmask)
      {},// no dependency flags
      nullptr,// memory barriers
      nullptr,// buffer memory barriers
      memory_barrier// image memory barriers
    );
}

void Kataglyphis::VulkanImage::setImage(vk::Image in_image) { this->image = in_image; }

void Kataglyphis::VulkanImage::cleanUp()
{
    if (device != nullptr) {
        if (image) { device->getLogicalDevice().destroyImage(image); }
        if (imageMemory) { device->getLogicalDevice().freeMemory(imageMemory); }
    }

    image = nullptr;
    imageMemory = nullptr;
}

Kataglyphis::VulkanImage::~VulkanImage() = default;

auto Kataglyphis::VulkanImage::accessFlagsForImageLayout(vk::ImageLayout layout) -> vk::AccessFlags
{
    switch (layout) {
    case vk::ImageLayout::ePreinitialized:
        return vk::AccessFlagBits::eHostWrite;
    case vk::ImageLayout::eTransferDstOptimal:
        return vk::AccessFlagBits::eTransferWrite;
    case vk::ImageLayout::eTransferSrcOptimal:
        return vk::AccessFlagBits::eTransferRead;
    case vk::ImageLayout::eColorAttachmentOptimal:
        return vk::AccessFlagBits::eColorAttachmentWrite;
    case vk::ImageLayout::eDepthStencilAttachmentOptimal:
        return vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
        return vk::AccessFlagBits::eShaderRead;
    case vk::ImageLayout::eGeneral:
        return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
               | vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eTransferRead
               | vk::AccessFlagBits::eTransferWrite;
    default:
        return vk::AccessFlags();
    }
}

auto Kataglyphis::VulkanImage::pipelineStageForLayout(vk::ImageLayout oldImageLayout) -> vk::PipelineStageFlags
{
    switch (oldImageLayout) {
    case vk::ImageLayout::eTransferDstOptimal:
    case vk::ImageLayout::eTransferSrcOptimal:
        return vk::PipelineStageFlagBits::eTransfer;
    case vk::ImageLayout::eColorAttachmentOptimal:
        return vk::PipelineStageFlagBits::eColorAttachmentOutput;
    case vk::ImageLayout::eDepthStencilAttachmentOptimal:
        return vk::PipelineStageFlagBits::eAllCommands;// We do this to allow queue
                                                       // other than graphic return
                                                       // vk::PipelineStageFlagBits::eEarlyFragmentTests;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
        return vk::PipelineStageFlagBits::eAllCommands;// We do this to allow queue
                                                       // other than graphic return
                                                       // vk::PipelineStageFlagBits::eFragmentShader;
    case vk::ImageLayout::ePreinitialized:
        return vk::PipelineStageFlagBits::eHost;
    case vk::ImageLayout::eUndefined:
        return vk::PipelineStageFlagBits::eTopOfPipe;
    case vk::ImageLayout::eGeneral:
        return vk::PipelineStageFlagBits::eAllCommands;
    default:
        return vk::PipelineStageFlagBits::eBottomOfPipe;
    }
}
