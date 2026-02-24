#pragma once
#include "renderer/VulkanRendererConfig.hpp"
#include "spdlog/spdlog.h"
#include <filesystem>
#include <string>
#include <vector>

namespace Kataglyphis::ShaderIncludes {

std::string getShaderIncludes()
{
    spdlog::default_logger_raw()->log(
      spdlog::level::info, std::string("The shader includes are the following: ") + ShaderIncludesString);

    return ShaderIncludesString;
}

}// namespace Kataglyphis::ShaderIncludes
