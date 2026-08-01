module;

#include <string>
#include <vector>
#include <memory>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.texture;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.image;
import kataglyphis.vulkan.image_view;

export namespace Kataglyphis {
class Texture
{
  public:
    Texture();
    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;
    Texture(Texture &&other) noexcept;
    Texture &operator=(Texture &&other) noexcept;

    bool createFromFile(std::shared_ptr<VulkanDevice>device, vk::CommandPool commandPool, const std::string &fileName);
    /// Decodes an encoded image (PNG/JPG/...) already in memory and uploads it,
    /// for glTF's embedded / data-URI images which never touch the filesystem.
    /// Shares the upload path with createFromFile; returns false if the bytes
    /// do not decode.
    bool createFromMemory(std::shared_ptr<VulkanDevice>device,
      vk::CommandPool commandPool,
      const unsigned char *encodedBytes,
      size_t byteCount);
    void createDefaultTexture(std::shared_ptr<VulkanDevice>device, vk::CommandPool commandPool);

    void setImage(vk::Image image);
    void setImageView(vk::ImageView imageView);

    uint32_t getMipLevel() const { return mip_levels; };
    VulkanImage &getVulkanImage() { return vulkanImage; };
    vk::Image &getImage() { return vulkanImage.getImage(); };
    vk::ImageView &getImageView() { return vulkanImageView.getImageView(); };
    vk::Sampler &getSampler() { return textureSampler; }

    void createImage(std::shared_ptr<VulkanDevice>device,
      uint32_t width,
      uint32_t height,
      uint32_t in_mip_levels,
      vk::Format format,
      vk::ImageTiling tiling,
      vk::ImageUsageFlags use_flags,
      vk::MemoryPropertyFlags prop_flags,
      uint32_t array_layers = 1,
      vk::ImageCreateFlags create_flags = {},
      vk::ImageType image_type = vk::ImageType::e2D,
      uint32_t depth = 1);

    void createImageView(std::shared_ptr<VulkanDevice>device, vk::Format format, vk::ImageAspectFlags aspect_flags, uint32_t in_mip_levels, vk::ImageViewType view_type = vk::ImageViewType::e2D, uint32_t array_layers = 1);

    void createTextureSampler(std::shared_ptr<VulkanDevice>device,
      vk::Filter filter = vk::Filter::eLinear,
      vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eRepeat,
      vk::Bool32 compareEnable = VK_FALSE,
      vk::CompareOp compareOp = vk::CompareOp::eAlways);

    void cleanUp();

    // Decodes an image file to tightly packed RGBA8. Returns nullptr on
    // failure; on success `*image_size` is exactly width * height * 4 and the
    // caller owns the buffer (free with stbi_image_free).
    //
    // Public because it is a stateless utility that needs no Vulkan device,
    // and because it is the surface texture_loading_fuzz_test exercises: the
    // byte count it reports sizes a staging buffer that is then memcpy'd into,
    // so a mismatch between the count and the returned allocation is a heap
    // overflow rather than a bad picture.
    static unsigned char *loadTextureData(const std::string &file_name, int *width, int *height, vk::DeviceSize *image_size);

    ~Texture();

  private:
    // Shared tail of createFromFile / createFromMemory: takes tightly packed
    // RGBA8 and does the staging-buffer copy, image creation, layout
    // transitions, mip generation and view creation. Returns false on an empty
    // image.
    bool uploadRgba(std::shared_ptr<VulkanDevice>device,
      vk::CommandPool commandPool,
      int width,
      int height,
      vk::DeviceSize size,
      const unsigned char *rgba);

    uint32_t mip_levels = 0;

    void generateMipMaps(vk::CommandBuffer command_buffer, vk::Image image, int32_t width, int32_t height);

    VulkanImage vulkanImage;
    VulkanImageView vulkanImageView;
    vk::Sampler textureSampler = nullptr;
    std::shared_ptr<VulkanDevice>device = nullptr;
};
}// namespace Kataglyphis
