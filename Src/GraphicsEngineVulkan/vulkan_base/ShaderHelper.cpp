module;
#include <memory>

#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "spdlog/spdlog.h"

module kataglyphis.vulkan.shader_helper;

import kataglyphis.vulkan.device;

Kataglyphis::ShaderHelper::ShaderHelper() = default;

auto Kataglyphis::ShaderHelper::createShaderModule(std::shared_ptr<VulkanDevice>device, const std::vector<char> &code)
  -> vk::ShaderModule
{
    // shader module create info
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

    return shader_module_result.value;
}

Kataglyphis::ShaderHelper::~ShaderHelper() = default;
