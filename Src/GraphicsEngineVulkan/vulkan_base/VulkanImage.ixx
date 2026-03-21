module;
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.image;

import kataglyphis.vulkan.command_buffer_manager;
import kataglyphis.vulkan.device;

export namespace Kataglyphis {
class VulkanImage
{
  public:
    VulkanImage();
    VulkanImage(const VulkanImage &) = delete;
    VulkanImage &operator=(const VulkanImage &) = delete;
    VulkanImage(VulkanImage &&other) noexcept;
    VulkanImage &operator=(VulkanImage &&other) noexcept;

    void create(VulkanDevice *in_device,
      uint32_t width,
      uint32_t height,
      uint32_t mip_levels,
      vk::Format format,
      vk::ImageTiling tiling,
      vk::ImageUsageFlags use_flags,
      vk::MemoryPropertyFlags prop_flags);

    void transitionImageLayout(vk::Device in_logical_device,
      vk::Queue queue,
      vk::CommandPool command_pool,
      vk::ImageLayout old_layout,
      vk::ImageLayout new_layout,
      vk::ImageAspectFlags aspectMask,
      uint32_t mip_levels);

    void transitionImageLayout(vk::CommandBuffer command_buffer,
      vk::ImageLayout old_layout,
      vk::ImageLayout new_layout,
      uint32_t mip_levels,
      vk::ImageAspectFlags aspectMask);

    void setImage(vk::Image image);
    vk::Image &getImage() { return image; };

    void cleanUp();

    ~VulkanImage();

  private:
    VulkanDevice *device{ nullptr };
    Kataglyphis::VulkanRendererInternals::CommandBufferManager commandBufferManager;

    vk::Image image{};
    vk::DeviceMemory imageMemory{};

    static vk::AccessFlags accessFlagsForImageLayout(vk::ImageLayout layout);
    static vk::PipelineStageFlags pipelineStageForLayout(vk::ImageLayout oldImageLayout);
};
}// namespace Kataglyphis
