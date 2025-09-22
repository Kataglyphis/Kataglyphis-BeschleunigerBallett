#pragma once
#include "renderer/VulkanRendererConfig.hpp"
#include "spdlog/spdlog.h"
#include <filesystem>
#include <string>
#include <vector>

namespace Kataglyphis::ShaderIncludes {

std::string getShaderIncludes()
{
    spdlog::info("The shader includes are the following: {}", ShaderIncludesString);

    return ShaderIncludesString;
}

}// namespace Kataglyphis::ShaderIncludes
