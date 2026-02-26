module;
#include <vulkan/vulkan.h>

export module kataglyphis.vulkan.image;

import kataglyphis.vulkan.command_buffer_manager;
import kataglyphis.vulkan.device;

export namespace Kataglyphis {
class VulkanImage
{
  public:
    VulkanImage();
    VulkanImage(const VulkanImage &) = delete;
    auto operator=(const VulkanImage &) -> VulkanImage & = delete;
    VulkanImage(VulkanImage &&other) noexcept;
    auto operator=(VulkanImage &&other) noexcept -> VulkanImage &;

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
    Kataglyphis::VulkanRendererInternals::CommandBufferManager commandBufferManager;

    VkImage image{};
    VkDeviceMemory imageMemory{};

    static VkAccessFlags accessFlagsForImageLayout(VkImageLayout layout);
    static VkPipelineStageFlags pipelineStageForLayout(VkImageLayout oldImageLayout);
};
}// namespace Kataglyphis
