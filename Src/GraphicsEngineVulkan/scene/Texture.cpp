module;

#include "spdlog/spdlog.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stb_image.h>
#include <string>
#include <vulkan/vulkan.hpp>

module kataglyphis.vulkan.texture;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.buffer_manager;
import kataglyphis.vulkan.image;
import kataglyphis.vulkan.image_view;
import kataglyphis.vulkan.command_buffer_manager;

using namespace Kataglyphis;

Kataglyphis::Texture::Texture() = default;

namespace {
auto supportsLinearBlit(vk::PhysicalDevice physical_device, vk::Format image_format) -> bool
{
    vk::FormatProperties format_properties = physical_device.getFormatProperties(image_format);
    return (format_properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)
           != vk::FormatFeatureFlags{};
}
}// namespace

void Kataglyphis::Texture::createFromFile(VulkanDevice *device,
  vk::CommandPool commandPool,
  const std::string &fileName)
{
    int width = 0;
    int height = 0;
    vk::DeviceSize size = 0;
    unsigned char *image_data = loadTextureData(fileName, &width, &height, &size);

    constexpr vk::Format texture_format = vk::Format::eR8G8B8A8Unorm;
    mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    if (!supportsLinearBlit(device->getPhysicalDevice(), texture_format)) {
        spdlog::warn("Linear blit not supported for texture format; using single mip level.");
        mip_levels = 1;
    }

    VulkanBuffer stagingBuffer;
    stagingBuffer.create(device,
      size,
      vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void *data = nullptr;
    data = device->getLogicalDevice().mapMemory(stagingBuffer.getBufferMemory(), 0, size).value;
    memcpy(data, image_data, static_cast<size_t>(size));
    device->getLogicalDevice().unmapMemory(stagingBuffer.getBufferMemory());

    stbi_image_free(image_data);

    createImage(device,
      static_cast<uint32_t>(width),
      static_cast<uint32_t>(height),
      mip_levels,
      texture_format,
      vk::ImageTiling::eOptimal,
      vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

    vulkanImage.transitionImageLayout(device->getLogicalDevice(),
      device->getGraphicsQueue(),
      commandPool,
      vk::ImageLayout::eUndefined,
      vk::ImageLayout::eTransferDstOptimal,
      vk::ImageAspectFlagBits::eColor,
      mip_levels);

    vulkanBufferManager.copyImageBuffer(device->getLogicalDevice(),
      device->getGraphicsQueue(),
      commandPool,
      stagingBuffer.getBuffer(),
      vulkanImage.getImage(),
      static_cast<uint32_t>(width),
      static_cast<uint32_t>(height));

    if (mip_levels > 1) {
        generateMipMaps(device->getPhysicalDevice(),
          device->getLogicalDevice(),
          commandPool,
          device->getGraphicsQueue(),
          vulkanImage.getImage(),
          texture_format,
          width,
          height,
          mip_levels);
    } else {
        vulkanImage.transitionImageLayout(device->getLogicalDevice(),
          device->getGraphicsQueue(),
          commandPool,
          vk::ImageLayout::eTransferDstOptimal,
          vk::ImageLayout::eShaderReadOnlyOptimal,
          vk::ImageAspectFlagBits::eColor,
          1);
    }

    stagingBuffer.cleanUp();

    createImageView(device, texture_format, vk::ImageAspectFlagBits::eColor, mip_levels, vk::ImageViewType::e2D, 1);
}

void Kataglyphis::Texture::setImage(vk::Image image) { vulkanImage.setImage(image); }

void Kataglyphis::Texture::setImageView(vk::ImageView imageView) { vulkanImageView.setImageView(imageView); }

void Kataglyphis::Texture::createImage(VulkanDevice *in_device,
  uint32_t width,
  uint32_t height,
  uint32_t in_mip_levels,
  vk::Format format,
  vk::ImageTiling tiling,
  vk::ImageUsageFlags use_flags,
  vk::MemoryPropertyFlags prop_flags,
  uint32_t array_layers,
  vk::ImageCreateFlags create_flags,
  vk::ImageType image_type,
  uint32_t depth)
{
    this->device = in_device;
    vulkanImage.create(in_device, width, height, in_mip_levels, format, tiling, use_flags, prop_flags, array_layers, create_flags, image_type, depth);
}

void Kataglyphis::Texture::createImageView(VulkanDevice *in_device,
  vk::Format format,
  vk::ImageAspectFlags aspect_flags,
  uint32_t in_mip_levels,
  vk::ImageViewType view_type,
  uint32_t array_layers)
{
    this->device = in_device;
    vulkanImageView.create(in_device, vulkanImage.getImage(), format, aspect_flags, in_mip_levels, view_type, array_layers);
}

void Kataglyphis::Texture::createTextureSampler(VulkanDevice *in_device, vk::Filter filter, vk::SamplerAddressMode addressMode)
{
    this->device = in_device;
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = vk::CompareOp::eAlways;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mip_levels);

    textureSampler = device->getLogicalDevice().createSampler(samplerInfo).value;
}

void Kataglyphis::Texture::cleanUp()
{
    if (textureSampler && device) {
        device->getLogicalDevice().destroySampler(textureSampler);
        textureSampler = nullptr;
    }
    vulkanImageView.cleanUp();
    vulkanImage.cleanUp();
}

Kataglyphis::Texture::~Texture() = default;

auto Kataglyphis::Texture::loadTextureData(const std::string &file_name,
  int *width,
  int *height,
  vk::DeviceSize *image_size) -> unsigned char *
{
    int channels = 0;
    unsigned char *image = stbi_load(file_name.c_str(), width, height, &channels, STBI_rgb_alpha);

    if (image == nullptr) { spdlog::error("Failed to load a texture file! (" + file_name + ")"); }

    *image_size = static_cast<vk::DeviceSize>(*width) * static_cast<vk::DeviceSize>(*height) * 4;

    return image;
}

void Kataglyphis::Texture::generateMipMaps(vk::PhysicalDevice physical_device,
  vk::Device device,
  vk::CommandPool command_pool,
  vk::Queue queue,
  vk::Image image,
  vk::Format image_format,
  int32_t width,
  int32_t height,
  [[maybe_unused]] uint32_t in_mip_levels)
{
    vk::FormatProperties formatProperties = physical_device.getFormatProperties(image_format);

    if ((formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)
        == vk::FormatFeatureFlags{}) {
        spdlog::error("Texture image format does not support linear blitting!");
    }

    vk::CommandBuffer command_buffer =
      Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(device, command_pool);

    vk::ImageMemoryBarrier barrier{};
    barrier.image = image;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t tmp_width = width;
    int32_t tmp_height = height;

    for (uint32_t i = 1; i < mip_levels; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

        command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
          vk::PipelineStageFlagBits::eTransfer,
          vk::DependencyFlags{},
          {},
          {},
          barrier);

        vk::ImageBlit blit{};

        blit.srcOffsets[0] = vk::Offset3D{ 0, 0, 0 };
        blit.srcOffsets[1] = vk::Offset3D{ tmp_width, tmp_height, 1 };
        blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;

        blit.dstOffsets[0] = vk::Offset3D{ 0, 0, 0 };
        blit.dstOffsets[1] = vk::Offset3D{ tmp_width > 1 ? tmp_width / 2 : 1, tmp_height > 1 ? tmp_height / 2 : 1, 1 };
        blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        command_buffer.blitImage(image,
          vk::ImageLayout::eTransferSrcOptimal,
          image,
          vk::ImageLayout::eTransferDstOptimal,
          blit,
          vk::Filter::eLinear);

        barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
          vk::PipelineStageFlagBits::eFragmentShader,
          vk::DependencyFlags{},
          {},
          {},
          barrier);

        if (tmp_width > 1) { tmp_width /= 2; }
        if (tmp_height > 1) { tmp_height /= 2; }
    }

    barrier.subresourceRange.baseMipLevel = mip_levels - 1;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
      vk::PipelineStageFlagBits::eFragmentShader,
      vk::DependencyFlags{},
      {},
      {},
      barrier);

    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(
      device, command_pool, queue, command_buffer);
}