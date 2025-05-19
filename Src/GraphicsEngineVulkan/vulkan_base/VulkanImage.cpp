#include "vulkan_base/VulkanImage.hpp"

#include "common/MemoryHelper.hpp"
#include "common/Utilities.hpp"

VulkanImage::VulkanImage() {}

void VulkanImage::create(VulkanDevice *device,
  uint32_t width,
  uint32_t height,
  uint32_t mip_levels,
  VkFormat format,
  VkImageTiling tiling,
  VkImageUsageFlags use_flags,
  VkMemoryPropertyFlags prop_flags)
{
    this->device = device;
    // CREATE image
    // image creation info
    VkImageCreateInfo image_create_info{};
    image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_create_info.imageType = VK_IMAGE_TYPE_2D;// type of image (1D, 2D, 3D)
    image_create_info.extent.width = width;// width if image extent
    image_create_info.extent.height = height;// height if image extent
    image_create_info.extent.depth = 1;// height if image extent
    image_create_info.mipLevels = mip_levels;// number of mipmap levels
    image_create_info.arrayLayers = 1;// number of levels in image array
    image_create_info.format = format;// format type of image
    image_create_info.tiling = tiling;// tiling of image ("arranged" for optimal reading)
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;// layout of image data on creation
    image_create_info.usage = use_flags;// bit flags defining what image will be used for
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;// number of samples for multisampling
    image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;// whether image can be shared between queues

    VkResult result = vkCreateImage(device->getLogicalDevice(), &image_create_info, nullptr, &image);
    ASSERT_VULKAN(result, "Failed to create an image!")

    // CREATE memory for image
    // get memory requirements for a type of image
    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(device->getLogicalDevice(), image, &memory_requirements);

    // allocate memory using image requirements and user defined properties
    VkMemoryAllocateInfo memory_alloc_info{};
    memory_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory_alloc_info.allocationSize = memory_requirements.size;
    memory_alloc_info.memoryTypeIndex =
      find_memory_type_index(device->getPhysicalDevice(), memory_requirements.memoryTypeBits, prop_flags);

    result = vkAllocateMemory(device->getLogicalDevice(), &memory_alloc_info, nullptr, &imageMemory);
    ASSERT_VULKAN(result, "Failed to allocate memory!")

    // connect memory to image
    vkBindImageMemory(device->getLogicalDevice(), image, imageMemory, 0);
}

void VulkanImage::transitionImageLayout(VkDevice device,
  VkQueue queue,
  VkCommandPool command_pool,
  VkImageLayout old_layout,
  VkImageLayout new_layout,
  VkImageAspectFlags aspectMask,
  uint32_t mip_levels)
{
    VkCommandBuffer command_buffer = commandBufferManager.beginCommandBuffer(device, command_pool);

    // VK_IMAGE_ASPECT_COLOR_BIT
    VkImageMemoryBarrier memory_barrier{};
    memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
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

    VkPipelineStageFlags src_stage = pipelineStageForLayout(old_layout);
    VkPipelineStageFlags dst_stage = pipelineStageForLayout(new_layout);

    vkCmdPipelineBarrier(

      command_buffer,
      src_stage,
      dst_stage,// pipeline stages (match to src and dst accessmask)
      0,// no dependency flags
      0,
      nullptr,// memory barrier count + data
      0,
      nullptr,// buffer memory barrier count + data
      1,
      &memory_barrier// image memory barrier count + data

    );

    commandBufferManager.endAndSubmitCommandBuffer(device, command_pool, queue, command_buffer);
}

void VulkanImage::transitionImageLayout(VkCommandBuffer command_buffer,
  VkImageLayout old_layout,
  VkImageLayout new_layout,
  uint32_t mip_levels,
  VkImageAspectFlags aspectMask)
{
    VkImageMemoryBarrier memory_barrier{};
    memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
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

    VkPipelineStageFlags src_stage = pipelineStageForLayout(old_layout);
    VkPipelineStageFlags dst_stage = pipelineStageForLayout(new_layout);

    // if transitioning from new image to image ready to receive data

    vkCmdPipelineBarrier(

      command_buffer,
      src_stage,
      dst_stage,// pipeline stages (match to src and dst accessmask)
      0,// no dependency flags
      0,
      nullptr,// memory barrier count + data
      0,
      nullptr,// buffer memory barrier count + data
      1,
      &memory_barrier// image memory barrier count + data

    );
}

void VulkanImage::setImage(VkImage image) { this->image = image; }

void VulkanImage::cleanUp()
{
    vkDestroyImage(device->getLogicalDevice(), image, nullptr);
    vkFreeMemory(device->getLogicalDevice(), imageMemory, nullptr);
}

VulkanImage::~VulkanImage() {}

VkAccessFlags VulkanImage::accessFlagsForImageLayout(VkImageLayout layout)
{
    switch (layout) {
    case VK_IMAGE_LAYOUT_PREINITIALIZED:
        return VK_ACCESS_HOST_WRITE_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return VK_ACCESS_TRANSFER_WRITE_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return VK_ACCESS_TRANSFER_READ_BIT;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return VK_ACCESS_SHADER_READ_BIT;
    default:
        return VkAccessFlags();
    }
}

VkPipelineStageFlags VulkanImage::pipelineStageForLayout(VkImageLayout oldImageLayout)
{
    switch (oldImageLayout) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return VK_PIPELINE_STAGE_TRANSFER_BIT;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;// We do this to allow queue
                                                  // other than graphic return
                                                  // VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;// We do this to allow queue
                                                  // other than graphic return
                                                  // VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    case VK_IMAGE_LAYOUT_PREINITIALIZED:
        return VK_PIPELINE_STAGE_HOST_BIT;
    case VK_IMAGE_LAYOUT_UNDEFINED:
        return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    default:
        return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
}
