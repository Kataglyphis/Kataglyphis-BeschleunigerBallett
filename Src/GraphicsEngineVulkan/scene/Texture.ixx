module;

#include <stb_image.h>
#include <string>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.texture;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.buffer_manager;
import kataglyphis.vulkan.image;
import kataglyphis.vulkan.image_view;
import kataglyphis.vulkan.command_buffer_manager;

export namespace Kataglyphis {
class Texture
{
  public:
    Texture();
    Texture(const Texture &) = delete;
    auto operator=(const Texture &) -> Texture & = delete;
    Texture(Texture &&other) noexcept = default;
    auto operator=(Texture &&other) noexcept -> Texture & = default;

    void createFromFile(VulkanDevice *device, vk::CommandPool commandPool, const std::string &fileName);

    void setImage(vk::Image image);
    void setImageView(vk::ImageView imageView);

    uint32_t getMipLevel() const { return mip_levels; };
    VulkanImage &getVulkanImage() { return vulkanImage; };
    VulkanImageView &getVulkanImageView() { return vulkanImageView; };
    vk::Image &getImage() { return vulkanImage.getImage(); };
    vk::ImageView &getImageView() { return vulkanImageView.getImageView(); };

    void createImage(VulkanDevice *device,
      uint32_t width,
      uint32_t height,
      uint32_t in_mip_levels,
      vk::Format format,
      vk::ImageTiling tiling,
      vk::ImageUsageFlags use_flags,
      vk::MemoryPropertyFlags prop_flags);

    void createImageView(VulkanDevice *device, vk::Format format, vk::ImageAspectFlags aspect_flags, uint32_t in_mip_levels);

    void cleanUp();

    ~Texture();

  private:
    uint32_t mip_levels = 0;

    static stbi_uc *loadTextureData(const std::string &file_name, int *width, int *height, vk::DeviceSize *image_size);

    void generateMipMaps(vk::PhysicalDevice physical_device,
      vk::Device device,
      vk::CommandPool command_pool,
      vk::Queue queue,
      vk::Image image,
      vk::Format image_format,
      int32_t width,
      int32_t height,
      uint32_t in_mip_levels);

    Kataglyphis::VulkanRendererInternals::CommandBufferManager commandBufferManager;
    VulkanBufferManager vulkanBufferManager;

    VulkanImage vulkanImage;
    VulkanImageView vulkanImageView;
};
}// namespace Kataglyphis
