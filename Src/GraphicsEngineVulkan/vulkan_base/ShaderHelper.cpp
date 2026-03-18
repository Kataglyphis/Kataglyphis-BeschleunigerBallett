module;

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "vulkan_base/ShaderIncludes.hpp"

#include "spdlog/spdlog.h"

module kataglyphis.vulkan.shader_helper;

import kataglyphis.vulkan.config;

namespace {
constexpr int kShaderSearchMaxDepth = 8;

auto resolve_shader_source_path(const std::string &raw_shader_src_path) -> std::string
{
    std::error_code filesystem_error;
    std::filesystem::path const source_path(raw_shader_src_path);
    if (std::filesystem::exists(source_path, filesystem_error)) { return source_path.string(); }

    std::string normalized_raw_shader_src_path = raw_shader_src_path;
    std::ranges::replace(normalized_raw_shader_src_path, '\\', '/');

    const std::string marker = "Resources/Shaders/";
    const auto marker_pos = normalized_raw_shader_src_path.find(marker);
    if (marker_pos == std::string::npos) { return raw_shader_src_path; }

    const std::string relative_shader = normalized_raw_shader_src_path.substr(marker_pos + marker.size());
    auto current_path = std::filesystem::current_path(filesystem_error);
    if (filesystem_error) { return raw_shader_src_path; }

    for (int depth = 0; depth < kShaderSearchMaxDepth; ++depth) {
        const auto candidate = current_path / "Resources" / "Shaders" / relative_shader;
        if (std::filesystem::exists(candidate, filesystem_error)) { return candidate.string(); }

        if (filesystem_error || !current_path.has_parent_path()) { break; }

        current_path = current_path.parent_path();
    }

    return raw_shader_src_path;
}
}// namespace

Kataglyphis::ShaderHelper::ShaderHelper() = default;

void Kataglyphis::ShaderHelper::compileShader(const std::string &shader_src_dir, const std::string &shader_name)
{
    // GLSLC_EXE is set by cmake to the location of the vulkan glslc
    std::stringstream shader_src_path;
    std::stringstream shader_log_file;
    std::stringstream cmdShaderCompile;
    std::stringstream adminPriviliges;
    adminPriviliges << "runas /user:<admin-user> \"";

    // with wrapping your path with quotation marks one can use paths with blanks ...
    shader_src_path << shader_src_dir << shader_name;
    const auto resolved_shader_src_path = resolve_shader_source_path(shader_src_path.str());

    std::filesystem::path const resolved_shader_path_object(resolved_shader_src_path);
    std::filesystem::path const shader_spv_path_object =
      resolved_shader_path_object.parent_path() / "spv" / (resolved_shader_path_object.filename().string() + ".spv");
    std::error_code filesystem_error;
    std::filesystem::create_directories(shader_spv_path_object.parent_path(), filesystem_error);

    std::string shader_spv_path = shader_spv_path_object.string();
    shader_log_file << shader_src_dir << shader_name << ".log.txt";
    std::stringstream log_stdout_and_stderr;
    log_stdout_and_stderr << " > " << shader_log_file.str() << " 2> " << shader_log_file.str();

    cmdShaderCompile//<< adminPriviliges.str()
      << Kataglyphis::RendererConfig::glslcExe << target << std::quoted(resolved_shader_src_path) << " -o "
      << std::quoted(shader_spv_path) << " " << ShaderIncludes::getShaderIncludes();
    //<< log_stdout_and_stderr.str();

    spdlog::default_logger_raw()->log(
      spdlog::level::info, std::string("The shader compile command is the following: ") + cmdShaderCompile.str());
    // std::cout << cmdShaderCompile.str().c_str();

    system(cmdShaderCompile.str().c_str());
}

auto Kataglyphis::ShaderHelper::getShaderSpvDir(const std::string &shader_src_dir, const std::string &shader_name)
  -> std::string
{
    std::stringstream shader_src_path;
    shader_src_path << shader_src_dir << shader_name;
    const auto resolved_shader_src_path = resolve_shader_source_path(shader_src_path.str());

    std::filesystem::path const resolved_shader_path_object(resolved_shader_src_path);
    std::filesystem::path const shader_spv_path_object =
      resolved_shader_path_object.parent_path() / "spv" / (resolved_shader_path_object.filename().string() + ".spv");

    return shader_spv_path_object.string();
}

auto Kataglyphis::ShaderHelper::createShaderModule(VulkanDevice *device, const std::vector<char> &code)
  -> vk::ShaderModule
{
    // shader module create info
    vk::ShaderModuleCreateInfo shader_module_create_info{};
    shader_module_create_info.codeSize = code.size();// size of code
    shader_module_create_info.pCode = reinterpret_cast<const uint32_t *>(code.data());// pointer to code

    // C++ API throws on failure, no manual error check needed
    vk::ShaderModule shader_module = device->getLogicalDevice().createShaderModule(shader_module_create_info);

    return shader_module;
}

Kataglyphis::ShaderHelper::~ShaderHelper() = default;
