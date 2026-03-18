#pragma once

#include <stdexcept>

#include "hostDevice/host_device_shared_vars.hpp"
#include <spdlog/spdlog.h>

namespace Kataglyphis {
// Error checking on vulkan function calls - C++ API throws exceptions by default
// This macro is kept for compatibility but generally not needed with C++ API
#define ASSERT_VULKAN(val, error_string) \
    if (static_cast<vk::Result>(val) != vk::Result::eSuccess) { spdlog::error(error_string); }

#define NOT_YET_IMPLEMENTED spdlog::error("Not yet implemented!");

#ifdef NDEBUG
const bool ENABLE_VALIDATION_LAYERS = false;
#else
const bool ENABLE_VALIDATION_LAYERS = true;
#endif
}// namespace Kataglyphis