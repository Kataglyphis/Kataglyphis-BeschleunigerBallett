#pragma once

#include <cstdlib>

#include "host_device_shared_vars.hpp"
#include <spdlog/spdlog.h>

namespace Kataglyphis {
// Error checking on vulkan function calls. The project builds with
// exceptions disabled (/EHs-, VULKAN_HPP_NO_EXCEPTIONS), so the fail-fast
// primitive is abort(): a failed creation call must not continue into a
// null-handle dereference. Every current call site is a creation/
// allocation; none tolerates non-success.
#define ASSERT_VULKAN(val, error_string) \
    if (static_cast<vk::Result>(val) != vk::Result::eSuccess) { \
        spdlog::critical(error_string); \
        std::abort(); \
    }

#ifdef NDEBUG
const bool ENABLE_VALIDATION_LAYERS = false;
#else
const bool ENABLE_VALIDATION_LAYERS = true;
#endif
}// namespace Kataglyphis