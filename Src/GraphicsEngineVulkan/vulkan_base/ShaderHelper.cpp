module;
#include <memory>

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

// The baked glslc path is CMake-time truth: a binary built in the CI
// container carries the CONTAINER's scoop path, which does not exist on the
// host - system() then failed without anyone checking, and every runtime
// compile was a silent no-op that served the previous run's spv (found when
// a red-probe shader edit provably changed nothing on screen). Resolve at
// call time: the baked path when it exists, then VULKAN_SDK, then PATH.
auto resolve_glslc_executable() -> std::string
{
    std::error_code fs_error;
    const std::string baked{ Kataglyphis::RendererConfig::glslcExe };
    if (!baked.empty() && std::filesystem::exists(baked, fs_error) && !fs_error) { return baked; }

    if (const char *sdk = std::getenv("VULKAN_SDK"); sdk != nullptr) {
#ifdef _WIN32
        const std::filesystem::path candidate = std::filesystem::path(sdk) / "Bin" / "glslc.exe";
#else
        const std::filesystem::path candidate = std::filesystem::path(sdk) / "bin" / "glslc";
#endif
        if (std::filesystem::exists(candidate, fs_error) && !fs_error) { return candidate.string(); }
    }

    // Last resort: let the shell search PATH. If glslc is nowhere, the
    // system() return check at the call site reports it instead of silence.
    return "glslc";
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

    // Reuse the SPV only when it is NEWER than its source. The previous check
    // was existence-only, so once a .spv had been produced every later edit to
    // the .glsl was silently ignored - shader changes appeared to have no
    // effect and were impossible to iterate on.
    if (std::filesystem::exists(shader_spv_path, filesystem_error) && !filesystem_error) {
        std::error_code source_time_error;
        std::error_code spv_time_error;
        const auto source_time = std::filesystem::last_write_time(resolved_shader_src_path, source_time_error);
        const auto spv_time = std::filesystem::last_write_time(shader_spv_path, spv_time_error);

        const bool spv_is_current = !source_time_error && !spv_time_error && spv_time >= source_time;
        if (spv_is_current) {
            spdlog::default_logger_raw()->log(
              spdlog::level::info, std::string("SPV up to date, skipping runtime compile for: ") + shader_spv_path);
            return;
        }
        spdlog::default_logger_raw()->log(
          spdlog::level::info, std::string("SPV is older than its source, recompiling: ") + shader_spv_path);
    }

    // By default, disable runtime shader compilation in Release builds unless explicitly enabled
#ifndef KAT_ENABLE_RUNTIME_SHADER_COMPILATION
# ifdef NDEBUG
#  define KAT_ENABLE_RUNTIME_SHADER_COMPILATION 0
# else
#  define KAT_ENABLE_RUNTIME_SHADER_COMPILATION 1
# endif
#endif

#if KAT_ENABLE_RUNTIME_SHADER_COMPILATION
    shader_log_file << shader_src_dir << shader_name << ".log.txt";
    std::stringstream log_stdout_and_stderr;
    log_stdout_and_stderr << " > " << shader_log_file.str() << " 2> " << shader_log_file.str();

    cmdShaderCompile//<< adminPriviliges.str()
      << resolve_glslc_executable() << target << std::quoted(resolved_shader_src_path) << " -o "
      << std::quoted(shader_spv_path) << " " << ShaderIncludes::getShaderIncludes();

    spdlog::default_logger_raw()->log(
      spdlog::level::info, std::string("The shader compile command is the following: ") + cmdShaderCompile.str());

    const int compile_result = system(cmdShaderCompile.str().c_str());
    if (compile_result != 0) {
        // Loud, because the silent version of this cost a full debugging
        // cycle: the pipeline will consume whatever spv is already on disk.
        spdlog::error("Runtime shader compilation failed (exit {}) for '{}' - the existing spv at '{}' "
                      "will be used. Compiler output: '{}'.",
          compile_result,
          resolved_shader_src_path,
          shader_spv_path,
          shader_log_file.str());
    }
#else
    spdlog::default_logger_raw()->log(
      spdlog::level::warn,
      std::string("Runtime shader compilation disabled (release). Missing SPV: ") + shader_spv_path);
    return;
#endif
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

auto Kataglyphis::ShaderHelper::createShaderModule(std::shared_ptr<VulkanDevice>device, const std::vector<char> &code)
  -> vk::ShaderModule
{
    // shader module create info
    vk::ShaderModuleCreateInfo shader_module_create_info{};
    shader_module_create_info.codeSize = code.size();// size of code
    shader_module_create_info.pCode = reinterpret_cast<const uint32_t *>(code.data());// pointer to code

    // The old comment here claimed "the C++ API throws on failure" - it does
    // NOT: exceptions are disabled project-wide (VULKAN_HPP_NO_EXCEPTIONS), so
    // .value was taken regardless of .result and a failed createShaderModule
    // (bad SPIR-V, OOM) returned a null module that failed pipeline creation
    // opaquely later. Check and fail fast, matching ASSERT_VULKAN's semantics.
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
