module;

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vulkan/vulkan.hpp>

module kataglyphis.vulkan.sampler_builder;

auto Kataglyphis::buildSamplerCreateInfo(vk::Filter filter,
  vk::SamplerAddressMode addressMode,
  float maxLod,
  vk::Bool32 anisotropyEnable,
  float maxAnisotropy,
  vk::BorderColor borderColor,
  vk::Bool32 compareEnable,
  vk::CompareOp compareOp) -> vk::SamplerCreateInfo
{
    vk::SamplerCreateInfo sampler_create_info{};
    sampler_create_info.magFilter = filter;
    sampler_create_info.minFilter = filter;
    sampler_create_info.addressModeU = addressMode;
    sampler_create_info.addressModeV = addressMode;
    sampler_create_info.addressModeW = addressMode;
    sampler_create_info.borderColor = borderColor;
    sampler_create_info.unnormalizedCoordinates = VK_FALSE;
    sampler_create_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
    sampler_create_info.mipLodBias = 0.0F;
    sampler_create_info.minLod = 0.0F;
    sampler_create_info.maxLod = maxLod;
    sampler_create_info.anisotropyEnable = anisotropyEnable;
    sampler_create_info.maxAnisotropy = maxAnisotropy;
    sampler_create_info.compareEnable = compareEnable;
    sampler_create_info.compareOp = compareOp;

    return sampler_create_info;
}

auto Kataglyphis::findSamplerForMipLevel(std::span<const uint32_t> createdMipLevels, uint32_t mipLevel)
  -> std::optional<std::size_t>
{
    const auto it = std::ranges::find(createdMipLevels, mipLevel);
    if (it == createdMipLevels.end()) { return std::nullopt; }
    return static_cast<std::size_t>(std::ranges::distance(createdMipLevels.begin(), it));
}
