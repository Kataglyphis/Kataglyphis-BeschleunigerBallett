module;

#include <cstdint>
#include <optional>
#include <span>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.sampler_builder;

export namespace Kataglyphis {

// Builds a vk::SamplerCreateInfo from explicit parameters. Returns the struct
// only - it does not create the sampler, since the three call sites use three
// different createSampler overloads and error conventions.
auto buildSamplerCreateInfo(vk::Filter filter,
  vk::SamplerAddressMode addressMode,
  float maxLod,
  vk::Bool32 anisotropyEnable,
  float maxAnisotropy,
  vk::BorderColor borderColor,
  vk::Bool32 compareEnable = VK_FALSE,
  vk::CompareOp compareOp = vk::CompareOp::eNever) -> vk::SamplerCreateInfo;

// Looks up an already-created sampler by mip level so callers with several
// textures that only differ by mip count can reuse one vk::Sampler instead of
// allocating a duplicate per texture. Pure and device-free, so it is
// unit-testable without a Vulkan instance.
auto findSamplerForMipLevel(std::span<const uint32_t> createdMipLevels, uint32_t mipLevel)
  -> std::optional<std::size_t>;

}// namespace Kataglyphis
