#include "app/App.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <system_error>
#include <vector>

#include "spdlog/common.h"
#include "spdlog/logger.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"
#include <iostream>

extern "C" {
auto rusty_extern_c_integer() -> int32_t;
}

namespace {
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

auto main() -> int
{
    initialize_logging();

    if (USE_RUST) {
        const auto value = rusty_extern_c_integer();
        std::cout << "A value given directly by extern c function " << value << "\n";
        spdlog::info("Rust extern value: {}", value);
    }

    Kataglyphis::App application;
    return application.run();
}
