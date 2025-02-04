#pragma once

#include <stdexcept>

#include "hostDevice/host_device_shared_vars.hpp"
#include <spdlog/spdlog.h>

// Error checking on vulkan function calls
#define ASSERT_VULKAN(val, error_string) \
    if (val != VK_SUCCESS) { spdlog::error(error_string); }

#define NOT_YET_IMPLEMENTED spdlog::error("Not yet implemented!");

#ifdef NDEBUG
const bool ENABLE_VALIDATION_LAYERS = false;
#else
const bool ENABLE_VALIDATION_LAYERS = true;
#endif
