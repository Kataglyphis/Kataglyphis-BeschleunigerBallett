#include "scene/Texture.hpp"

#include "common/Utilities.hpp"
#include "spdlog/spdlog.h"
#include <cmath>
#include <stdexcept>

using namespace Kataglyphis;

Kataglyphis::Texture::Texture() {}

void Kataglyphis::Texture::createFromFile(VulkanDevice *device, VkCommandPool commandPool, const std::string &fileName)
{
    int width, height;
    VkDeviceSize size;
    stbi_uc *image_data = loadTextureData(fileName, &width, &height, &size);

    mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    // create staging buffer to hold loaded data, ready to copy to device
    VulkanBuffer stagingBuffer;
    stagingBuffer.create(device,
      size,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // copy image data to staging buffer
    void *data;
    vkMapMemory(device->getLogicalDevice(), stagingBuffer.getBufferMemory(), 0, size, 0, &data);
    memcpy(data, image_data, static_cast<size_t>(size));
    vkUnmapMemory(device->getLogicalDevice(), stagingBuffer.getBufferMemory());

    // free original image data
    stbi_image_free(image_data);

    createImage(device,
      width,
      height,
      mip_levels,
      VK_FORMAT_R8G8B8A8_UNORM,
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // copy data to image
    // transition image to be DST for copy operation
    vulkanImage.transitionImageLayout(device->getLogicalDevice(),
      device->getGraphicsQueue(),
      commandPool,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT,
      mip_levels);

    // copy data to image
    vulkanBufferManager.copyImageBuffer(device->getLogicalDevice(),
      device->getGraphicsQueue(),
      commandPool,
      stagingBuffer.getBuffer(),
      vulkanImage.getImage(),
      width,
      height);

    // generate mipmaps
    generateMipMaps(device->getPhysicalDevice(),
      device->getLogicalDevice(),
      commandPool,
      device->getGraphicsQueue(),
      vulkanImage.getImage(),
      VK_FORMAT_R8G8B8A8_SRGB,
      width,
      height,
      mip_levels);

    stagingBuffer.cleanUp();

    createImageView(device, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, mip_levels);
}

void Kataglyphis::Texture::setImage(VkImage image) { vulkanImage.setImage(image); }

void Kataglyphis::Texture::setImageView(VkImageView imageView) { vulkanImageView.setImageView(imageView); }

void Kataglyphis::Texture::createImage(VulkanDevice *device,
  uint32_t width,
  uint32_t height,
  uint32_t mip_levels,
  VkFormat format,
  VkImageTiling tiling,
  VkImageUsageFlags use_flags,
  VkMemoryPropertyFlags prop_flags)
{
    vulkanImage.create(device, width, height, mip_levels, format, tiling, use_flags, prop_flags);
}

void Kataglyphis::Texture::createImageView(VulkanDevice *device,
  VkFormat format,
  VkImageAspectFlags aspect_flags,
  uint32_t mip_levels)
{
    vulkanImageView.create(device, vulkanImage.getImage(), format, aspect_flags, mip_levels);
}

void Kataglyphis::Texture::cleanUp()
{
    vulkanImageView.cleanUp();
    vulkanImage.cleanUp();
}

Kataglyphis::Texture::~Texture() {}

stbi_uc *
  Kataglyphis::Texture::loadTextureData(const std::string &file_name, int *width, int *height, VkDeviceSize *image_size)
{
    // number of channels image uses
    int channels;
    // load pixel data for image
    // std::string file_loc = "../Resources/Textures/" + file_name;
    stbi_uc *image = stbi_load(file_name.c_str(), width, height, &channels, STBI_rgb_alpha);

    if (!image) { spdlog::error("Failed to load a texture file! (" + file_name + ")"); }

    // calculate image size using given and known data
    *image_size = *width * *height * 4;

    return image;
}

void Kataglyphis::Texture::generateMipMaps(VkPhysicalDevice physical_device,
  VkDevice device,
  VkCommandPool command_pool,
  VkQueue queue,
  VkImage image,
  VkFormat image_format,
  int32_t width,
  int32_t height,
  uint32_t mip_levels)
{
    // Check if image format supports linear blitting
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(physical_device, image_format, &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        spdlog::error("Texture image format does not support linear blitting!");
    }

    VkCommandBuffer command_buffer = commandBufferManager.beginCommandBuffer(device, command_pool);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    // TEMP VARS needed for decreasing step by step for factor 2
    int32_t tmp_width = width;
    int32_t tmp_height = height;

    // -- WE START AT 1 !
    for (uint32_t i = 1; i < mip_levels; i++) {
        // WAIT for previous mip map level for being ready
        barrier.subresourceRange.baseMipLevel = i - 1;
        // HERE we TRANSITION for having a SRC format now
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(command_buffer,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          0,
          0,
          nullptr,
          0,
          nullptr,
          1,
          &barrier);

        // when barrier over we can now blit :)
        VkImageBlit blit{};

        // -- OFFSETS describing the 3D-dimesnion of the region
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { tmp_width, tmp_height, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        // copy from previous level
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        // -- OFFSETS describing the 3D-dimesnion of the region
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { tmp_width > 1 ? tmp_width / 2 : 1, tmp_height > 1 ? tmp_height / 2 : 1, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        // -- COPY to next mipmap level
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(command_buffer,
          image,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          image,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          1,
          &blit,
          VK_FILTER_LINEAR);

        // REARRANGE image formats for having the correct image formats again
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(command_buffer,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          0,
          0,
          nullptr,
          0,
          nullptr,
          1,
          &barrier);

        if (tmp_width > 1) tmp_width /= 2;
        if (tmp_height > 1) tmp_height /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mip_levels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(command_buffer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &barrier);

    commandBufferManager.endAndSubmitCommandBuffer(device, command_pool, queue, command_buffer);
}
