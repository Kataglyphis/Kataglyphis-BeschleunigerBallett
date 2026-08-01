module;
#include <memory>

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "spdlog/spdlog.h"

module kataglyphis.vulkan.shader_helper;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.file;

namespace {

constexpr uint32_t kSpirvMagic = 0x07230203;

auto createShaderModuleFromBytes(const std::shared_ptr<Kataglyphis::VulkanDevice> &device, std::span<const char> code)
  -> vk::ShaderModule
{
    vk::ShaderModuleCreateInfo shader_module_create_info{};
    shader_module_create_info.codeSize = code.size();// size of code
    shader_module_create_info.pCode = reinterpret_cast<const uint32_t *>(code.data());// pointer to code

    auto shader_module_result = device->getLogicalDevice().createShaderModule(shader_module_create_info);
    if (shader_module_result.result != vk::Result::eSuccess) {
        spdlog::default_logger_raw()->log(spdlog::level::critical,
          std::string("Failed to create shader module (result ")
            + std::to_string(static_cast<int>(shader_module_result.result)) + ")");
        std::abort();
    }

    return shader_module_result.value;// UNCHECKED_VULKAN_RESULT_OK: already-fatal-abort-above
}

}// namespace

auto Kataglyphis::validateSpirvBlob(std::span<const char> code) -> bool
{
    if (code.empty() || code.size() % sizeof(uint32_t) != 0) { return false; }

    uint32_t magic = 0;
    std::memcpy(&magic, code.data(), sizeof(magic));
    return magic == kSpirvMagic;
}

auto Kataglyphis::loadSpirvShaderModule(std::shared_ptr<VulkanDevice> device, const std::string &spvPath)
  -> vk::ShaderModule
{
    const std::vector<char> code = File(spvPath).readCharSequence();

    if (!validateSpirvBlob(code)) {
        spdlog::default_logger_raw()->log(spdlog::level::critical,
          std::string("Invalid or missing SPIR-V shader blob at '") + spvPath
            + "' - run Scripts/Windows/compile-slang-shaders.ps1 (or the Linux .sh) to (re)compile shaders.");
        std::abort();
    }

    return createShaderModuleFromBytes(device, code);
}

Kataglyphis::ShaderStagePair::ShaderStagePair(std::shared_ptr<VulkanDevice> device, const std::string &vertexSpvPath,
  const std::string &fragmentSpvPath)
  : device_(std::move(device)), vertexModule_(loadSpirvShaderModule(device_, vertexSpvPath)),
    fragmentModule_(loadSpirvShaderModule(device_, fragmentSpvPath))
{
    stages_[0].stage = vk::ShaderStageFlagBits::eVertex;
    stages_[0].module = vertexModule_;
    stages_[0].pName = "main";

    stages_[1].stage = vk::ShaderStageFlagBits::eFragment;
    stages_[1].module = fragmentModule_;
    stages_[1].pName = "main";
}

Kataglyphis::ShaderStagePair::~ShaderStagePair()
{
    if (device_) {
        device_->getLogicalDevice().destroyShaderModule(vertexModule_);
        device_->getLogicalDevice().destroyShaderModule(fragmentModule_);
    }
}
