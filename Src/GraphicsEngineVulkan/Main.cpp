import kataglyphis.vulkan.app;

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <span>
#include <system_error>
#include <vector>

#if USE_RUST
#include "kataglyphis_rustprojecttemplate_bridge/lib.h"
#endif
#include "spdlog/common.h"
#include "spdlog/logger.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"
#include <iostream>
#include <string>

namespace {
auto normalize_gpu_mode(std::string value) -> std::string
{
    for (auto &character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    if (value == "auto" || value == "dedicated" || value == "integrated") { return value; }
    return "";
}

void apply_gpu_selection_from_args(std::span<char *const> arguments)
{
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string argument = arguments[index];
        const std::string prefix = "--gpu=";
        if (argument.rfind(prefix, 0) != 0) { continue; }

        const std::string gpu_mode = normalize_gpu_mode(argument.substr(prefix.size()));
        if (gpu_mode.empty()) {
            spdlog::warn("Invalid value for --gpu. Valid values are: auto, dedicated, integrated.");
            return;
        }

#if defined(_WIN32)
        _putenv_s("KATAGLYPHIS_VK_GPU", gpu_mode.c_str());
#else
        setenv("KATAGLYPHIS_VK_GPU", gpu_mode.c_str(), 1);
#endif

        spdlog::default_logger_raw()->log(
          spdlog::level::info, std::string("GPU selection mode set via CLI: ") + gpu_mode);
        return;
    }
}

void initialize_logging()
{
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);

    std::vector<spdlog::sink_ptr> sinks{ console_sink };

    std::error_code fs_error;
    const auto current_path = std::filesystem::current_path(fs_error);
    if (!fs_error) {
        const auto log_dir = current_path / "logs" / "GraphicsEngineVulkan";
        std::filesystem::create_directories(log_dir, fs_error);

        if (!fs_error) {
            const auto log_file = log_dir / "graphics_engine_vulkan.log";
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file.string(), true);
            file_sink->set_level(spdlog::level::trace);
            sinks.push_back(file_sink);
        }
    }

    auto logger = std::make_shared<spdlog::logger>("GraphicsEngineVulkan", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::warn);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    spdlog::set_default_logger(logger);
    spdlog::info("Logger initialized.");
}
}// namespace

auto main(int argc, char **argv) -> int
{
    initialize_logging();
    apply_gpu_selection_from_args(std::span<char *const>(argv, static_cast<std::size_t>(argc)));

    #if USE_RUST
    if (USE_RUST) {
        const auto value = rusty_cxxbridge_integer();
        std::cout << "A value given by the Rust bridge function " << value << "\n";
        spdlog::default_logger_raw()->log(
          spdlog::level::info, std::string("Rust extern value: ") + std::to_string(value));
    }
#endif

    return Kataglyphis::App::run();
}
