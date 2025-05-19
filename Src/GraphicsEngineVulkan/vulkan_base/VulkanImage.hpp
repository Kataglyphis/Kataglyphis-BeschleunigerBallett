#pragma once
#include <vulkan/vulkan.h>

#include "renderer/CommandBufferManager.hpp"
#include "vulkan_base/VulkanDevice.hpp"

class VulkanImage
{
  public:
    VulkanImage();

    void create(VulkanDevice *device,
      uint32_t width,
      uint32_t height,
      uint32_t mip_levels,
      VkFormat format,
      VkImageTiling tiling,
      VkImageUsageFlags use_flags,
      VkMemoryPropertyFlags prop_flags);

    void transitionImageLayout(VkDevice device,
      VkQueue queue,
      VkCommandPool command_pool,
      VkImageLayout old_layout,
      VkImageLayout new_layout,
      VkImageAspectFlags aspectMask,
      uint32_t mip_levels);

    void transitionImageLayout(VkCommandBuffer command_buffer,
      VkImageLayout old_layout,
      VkImageLayout new_layout,
      uint32_t mip_levels,
      VkImageAspectFlags aspectMask);

    void setImage(VkImage image);
    VkImage &getImage() { return image; };

    void cleanUp();

    ~VulkanImage();

  private:
    VulkanDevice *device{ VK_NULL_HANDLE };
    CommandBufferManager commandBufferManager;

    VkImage image;
    VkDeviceMemory imageMemory;

    VkAccessFlags accessFlagsForImageLayout(VkImageLayout layout);
    VkPipelineStageFlags pipelineStageForLayout(VkImageLayout oldImageLayout);
};
