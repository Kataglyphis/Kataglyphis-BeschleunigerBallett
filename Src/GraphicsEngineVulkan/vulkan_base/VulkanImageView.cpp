module;

#include "common/Utilities.hpp"
#include <cstdint>
#include <utility>
#include <vulkan/vulkan.hpp>

module kataglyphis.vulkan.image_view;

import kataglyphis.vulkan.device;

Kataglyphis::VulkanImageView::VulkanImageView() = default;

Kataglyphis::VulkanImageView::VulkanImageView(VulkanImageView &&other) noexcept
  : device(other.device), imageView(other.imageView)
{
    other.device = nullptr;
    other.imageView = nullptr;
}

auto Kataglyphis::VulkanImageView::operator=(VulkanImageView &&other) noexcept -> VulkanImageView &
{
    if (this != &other) {
        cleanUp();

        device = other.device;
        imageView = other.imageView;

        other.device = nullptr;
        other.imageView = nullptr;
    }

    return *this;
}

void Kataglyphis::VulkanImageView::setImageView(vk::ImageView in_imageView) { this->imageView = in_imageView; }

void Kataglyphis::VulkanImageView::create(VulkanDevice *in_device,
  vk::Image image,
  vk::Format format,
  vk::ImageAspectFlags aspect_flags,
  uint32_t mip_levels,
  vk::ImageViewType view_type,
  uint32_t array_layers)
{
    this->device = in_device;

    vk::ImageViewCreateInfo view_create_info{};
    view_create_info.image = image;// image to create view for
    view_create_info.viewType = view_type;// typ of image
    view_create_info.format = format;
    view_create_info.components.r = vk::ComponentSwizzle::eIdentity;// allows remapping of rgba components to
                                                                    // other rgba values
    view_create_info.components.g = vk::ComponentSwizzle::eIdentity;
    view_create_info.components.b = vk::ComponentSwizzle::eIdentity;
    view_create_info.components.a = vk::ComponentSwizzle::eIdentity;

    // subresources allow the view to view only a part of an image
    view_create_info.subresourceRange.aspectMask = aspect_flags;// which aspect of an image to view (e.g. color bit for
                                                                // viewing color)

    view_create_info.subresourceRange.baseMipLevel = 0;// start mipmap level to view from
    view_create_info.subresourceRange.levelCount = mip_levels;// number of mipmap levels to view

    view_create_info.subresourceRange.baseArrayLayer = 0;// start array level to view from
    view_create_info.subresourceRange.layerCount = array_layers;// number of array levels to view

    // create image view
    imageView = device->getLogicalDevice().createImageView(view_create_info).value;
}

void Kataglyphis::VulkanImageView::cleanUp()
{
    if (device != nullptr && imageView) { device->getLogicalDevice().destroyImageView(imageView); }

    imageView = nullptr;
}

Kataglyphis::VulkanImageView::~VulkanImageView() = default;
