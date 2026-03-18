module;
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.image_view;

import kataglyphis.vulkan.device;

export namespace Kataglyphis {
class VulkanImageView
{
  public:
    VulkanImageView();
    VulkanImageView(const VulkanImageView &) = delete;
    auto operator=(const VulkanImageView &) -> VulkanImageView & = delete;
    VulkanImageView(VulkanImageView &&other) noexcept;
    auto operator=(VulkanImageView &&other) noexcept -> VulkanImageView &;

    void setImageView(vk::ImageView imageView);

    vk::ImageView &getImageView() { return imageView; };

    void create(VulkanDevice *in_device,
      vk::Image image,
      vk::Format format,
      vk::ImageAspectFlags aspect_flags,
      uint32_t mip_levels);

    void cleanUp();

    ~VulkanImageView();

  private:
    VulkanDevice *device{ nullptr };

    vk::ImageView imageView{};
};
}// namespace Kataglyphis
