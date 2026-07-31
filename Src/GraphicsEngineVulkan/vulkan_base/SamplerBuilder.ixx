module;

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
  vk::BorderColor borderColor) -> vk::SamplerCreateInfo;

}// namespace Kataglyphis
