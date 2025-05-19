#pragma once
#include <vulkan/vulkan.h>

#include "vulkan_base/VulkanDevice.hpp"

class VulkanImageView
{
  public:
    VulkanImageView();

    void setImageView(VkImageView imageView);

    VkImageView &getImageView() { return imageView; };

    void create(VulkanDevice *device,
      VkImage image,
      VkFormat format,
      VkImageAspectFlags aspect_flags,
      uint32_t mip_levels);

    void cleanUp();

    ~VulkanImageView();

  private:
    VulkanDevice *device{ VK_NULL_HANDLE };

    VkImageView imageView;
};
