module;
#include <memory>

#include <array>
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

// Owns the vertex + fragment vk::ShaderModule pair every pipeline-creation
// site loads, plus the two vk::PipelineShaderStageCreateInfo that reference
// them. Loads both modules via loadSpirvShaderModule (aborting exactly as
// that function does on a bad blob) and destroys both on destruction, so a
// call site no longer hand-writes the destroyShaderModule pair. Move-only
// would still leave the create-infos pointing at a module owned by a
// relocated instance for no benefit at these call sites (every use is a
// function-local), so copy AND move are both deleted instead.
class ShaderStagePair
{
  public:
    ShaderStagePair(std::shared_ptr<VulkanDevice> device, const std::string &vertexSpvPath,
      const std::string &fragmentSpvPath);

    ShaderStagePair(const ShaderStagePair &) = delete;
    ShaderStagePair &operator=(const ShaderStagePair &) = delete;
    ShaderStagePair(ShaderStagePair &&) = delete;
    ShaderStagePair &operator=(ShaderStagePair &&) = delete;

    ~ShaderStagePair();

    [[nodiscard]] auto stages() const -> std::span<const vk::PipelineShaderStageCreateInfo> { return stages_; }

  private:
    std::shared_ptr<VulkanDevice> device_;
    vk::ShaderModule vertexModule_{ nullptr };
    vk::ShaderModule fragmentModule_{ nullptr };
    std::array<vk::PipelineShaderStageCreateInfo, 2> stages_{};
};

}// namespace Kataglyphis
