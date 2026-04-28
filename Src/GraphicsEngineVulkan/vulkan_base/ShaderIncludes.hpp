#pragma once
#include "spdlog/spdlog.h"
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace Kataglyphis::ShaderIncludes {

std::string getShaderIncludes()
{
    std::string includes = ShaderIncludesString;

    std::filesystem::path first_include;
    std::istringstream iss(includes);
    std::string token;
    while (iss >> token) {
        if (token == "-I") {
            iss >> token;
            first_include = token;
            break;
        }
    }

    if (!first_include.empty() && !std::filesystem::exists(first_include)) {
        std::filesystem::path shaders_dir = std::filesystem::current_path() / "Resources" / "Shaders";
        if (std::filesystem::exists(shaders_dir)) {
            std::string runtime_includes =
              "-I " + shaders_dir.string() +
              " -I " + (shaders_dir / "common").string() +
              " -I " + (shaders_dir / "pbr").string() +
              " -I " + (shaders_dir / "pbr" / "brdf").string() +
              " -I " + (shaders_dir / "hostDevice").string() +
              " -I " + std::filesystem::current_path().string() + "/Src/GraphicsEngineVulkan" +
              " -I " + std::filesystem::current_path().string() + "/Src/GraphicsEngineVulkan/renderer" +
              " -I " + std::filesystem::current_path().string() + "/Src/GraphicsEngineVulkan/renderer/pushConstants" +
              " -I " + std::filesystem::current_path().string() + "/Src/GraphicsEngineVulkan/scene";

            spdlog::default_logger_raw()->log(
              spdlog::level::warn,
              std::string("Hardcoded shader includes not found, using runtime paths: ") + runtime_includes);
            includes = runtime_includes;
        }
    }

    spdlog::default_logger_raw()->log(
      spdlog::level::info, std::string("The shader includes are the following: ") + includes);

    return includes;
}

}// namespace Kataglyphis::ShaderIncludes
