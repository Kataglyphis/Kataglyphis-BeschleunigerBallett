#include "vulkan_base/VulkanImageView.hpp"
#include "common/Utilities.hpp"

Kataglyphis::VulkanImageView::VulkanImageView() {}

void Kataglyphis::VulkanImageView::setImageView(VkImageView imageView) { this->imageView = imageView; }

void Kataglyphis::VulkanImageView::create(VulkanDevice *device,
  VkImage image,
  VkFormat format,
  VkImageAspectFlags aspect_flags,
  uint32_t mip_levels)
{
    this->device = device;

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
    VkResult result = vkCreateImageView(device->getLogicalDevice(), &view_create_info, nullptr, &imageView);
    ASSERT_VULKAN(result, "Failed to create an image view!")
}

void Kataglyphis::VulkanImageView::cleanUp() { vkDestroyImageView(device->getLogicalDevice(), imageView, nullptr); }

Kataglyphis::VulkanImageView::~VulkanImageView() {}
