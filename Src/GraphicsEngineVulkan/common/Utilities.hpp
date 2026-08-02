#pragma once

#include <cstdlib>

#include "host_device_shared_vars.hpp"
#include <spdlog/spdlog.h>

namespace Kataglyphis {
// Error checking on vulkan function calls. The project builds with
// exceptions disabled (/EHs-, VULKAN_HPP_NO_EXCEPTIONS), so the fail-fast
// primitive is abort(): a failed creation call must not continue into a
// null-handle dereference. Also used on a handful of query calls whose
// result feeds a decision with no safe fallback (e.g. surface capabilities
// sizing a swapchain) - none of those tolerate non-success either.
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