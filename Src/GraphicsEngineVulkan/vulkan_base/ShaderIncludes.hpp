#pragma once
#include "renderer/VulkanRendererConfig.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include "spdlog/spdlog.h"

namespace ShaderIncludes {

std::string getShaderIncludes()
{
    spdlog::info("The shader includes are the following: {}", ShaderIncludesString);

    return ShaderIncludesString;
}

}// namespace ShaderIncludes
