module;
#include <memory>

#include <span>
#include <string>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.shader_helper;

import kataglyphis.vulkan.device;

export namespace Kataglyphis {

// Structural check that `code` looks like a compiled SPIR-V module: non-empty,
// a multiple of 4 bytes, and starting with the SPIR-V magic number. Pure - no
// Vulkan calls - so it is unit-testable without a device.
auto validateSpirvBlob(std::span<const char> code) -> bool;

// Reads spvPath, validates it with validateSpirvBlob, and creates the
// vk::ShaderModule. A missing file or a blob that fails validation logs
// critical (with a hint to (re)compile shaders) and aborts - shader loading
// happens during stage init, where this codebase already treats a Vulkan
// creation failure as unrecoverable.
auto loadSpirvShaderModule(std::shared_ptr<VulkanDevice> device, const std::string &spvPath) -> vk::ShaderModule;

}// namespace Kataglyphis
