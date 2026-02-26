module;

#include <stb_image.h>
#include <string>
#include <vulkan/vulkan.h>

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

    void createFromFile(VulkanDevice *device, VkCommandPool commandPool, const std::string &fileName);

    void setImage(VkImage image);
    void setImageView(VkImageView imageView);

    uint32_t getMipLevel() const { return mip_levels; };
    VulkanImage &getVulkanImage() { return vulkanImage; };
    VulkanImageView &getVulkanImageView() { return vulkanImageView; };
    VkImage &getImage() { return vulkanImage.getImage(); };
    VkImageView &getImageView() { return vulkanImageView.getImageView(); };

    void createImage(VulkanDevice *device,
      uint32_t width,
      uint32_t height,
      uint32_t mip_levels,
      VkFormat format,
      VkImageTiling tiling,
      VkImageUsageFlags use_flags,
      VkMemoryPropertyFlags prop_flags);

    void createImageView(VulkanDevice *device, VkFormat format, VkImageAspectFlags aspect_flags, uint32_t mip_levels);

    void cleanUp();

    ~Texture();

  private:
    uint32_t mip_levels = 0;

    static stbi_uc *loadTextureData(const std::string &file_name, int *width, int *height, VkDeviceSize *image_size);

    void generateMipMaps(VkPhysicalDevice physical_device,
      VkDevice device,
      VkCommandPool command_pool,
      VkQueue queue,
      VkImage image,
      VkFormat image_format,
      int32_t width,
      int32_t height,
      uint32_t mip_levels);

    Kataglyphis::VulkanRendererInternals::CommandBufferManager commandBufferManager;
    VulkanBufferManager vulkanBufferManager;

    VulkanImage vulkanImage;
    VulkanImageView vulkanImageView;
};
}// namespace Kataglyphis
