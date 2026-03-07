module;

#include "common/Utilities.hpp"
#include <cstdint>
#include <utility>
#include <vulkan/vulkan.h>

module kataglyphis.vulkan.image_view;

import kataglyphis.vulkan.device;

Kataglyphis::VulkanImageView::VulkanImageView() = default;

Kataglyphis::VulkanImageView::VulkanImageView(VulkanImageView &&other) noexcept
  : device(other.device), imageView(other.imageView)
{
    other.device = VK_NULL_HANDLE;
    other.imageView = VK_NULL_HANDLE;
}

auto Kataglyphis::VulkanImageView::operator=(VulkanImageView &&other) noexcept -> VulkanImageView &
{
    if (this != &other) {
        cleanUp();

        device = other.device;
        imageView = other.imageView;

        other.device = VK_NULL_HANDLE;
        other.imageView = VK_NULL_HANDLE;
    }

    return *this;
}

void Kataglyphis::VulkanImageView::setImageView(VkImageView in_imageView) { this->imageView = in_imageView; }

void Kataglyphis::VulkanImageView::create(VulkanDevice *in_device,
  VkImage image,
  VkFormat format,
  VkImageAspectFlags aspect_flags,
  uint32_t mip_levels)
{
    this->device = in_device;

    VkImageViewCreateInfo view_create_info{};
    view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_create_info.image = image;// image to create view for
    view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;// typ of image
    view_create_info.format = format;
    view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;// allows remapping of rgba components to
                                                                  // other rgba values
    view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    // subresources allow the view to view only a part of an image
    view_create_info.subresourceRange.aspectMask = aspect_flags;// which aspect of an image to view (e.g. color bit for
                                                                // viewing color)
    view_create_info.subresourceRange.baseMipLevel = 0;// start mipmap level to view from
    view_create_info.subresourceRange.levelCount = mip_levels;// number of mipmap levels to view
    view_create_info.subresourceRange.baseArrayLayer = 0;// start array level to view from
    view_create_info.subresourceRange.layerCount = 1;// number of array levels to view

    // create image view
    VkResult const result = vkCreateImageView(device->getLogicalDevice(), &view_create_info, nullptr, &imageView);
    ASSERT_VULKAN(result, "Failed to create an image view!")
}

void Kataglyphis::VulkanImageView::cleanUp()
{
    if (device != VK_NULL_HANDLE && imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device->getLogicalDevice(), imageView, nullptr);
    }

    imageView = VK_NULL_HANDLE;
}

Kataglyphis::VulkanImageView::~VulkanImageView() = default;
