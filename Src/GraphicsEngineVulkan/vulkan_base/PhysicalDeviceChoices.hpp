#pragma once

#include <algorithm>
#include <cctype>
#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <vulkan/vulkan.hpp>

namespace Kataglyphis {

enum class GpuSelectionMode : std::uint8_t { Auto, Dedicated, Integrated };

// Device type strictly dominates: operator<=> compares typeRank first, so no
// amount of capability can let a lower device type outrank a higher one.
struct PhysicalDeviceScore {
    int typeRank;// 4 discrete, 3 integrated, 2 virtual, 1 cpu, 0 other
    uint32_t capability;// maxImageDimension2D, tie-break only
    friend auto operator<=>(const PhysicalDeviceScore &, const PhysicalDeviceScore &) = default;
};

inline auto parseGpuSelectionMode(std::string_view mode) -> GpuSelectionMode
{
    if (mode.empty()) { return GpuSelectionMode::Auto; }

    std::string lowered(mode);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (lowered == "dedicated") { return GpuSelectionMode::Dedicated; }
    if (lowered == "integrated") { return GpuSelectionMode::Integrated; }

    return GpuSelectionMode::Auto;
}

inline auto gpuSelectionModeToString(GpuSelectionMode mode) -> const char *
{
    switch (mode) {
    case GpuSelectionMode::Dedicated:
        return "dedicated";
    case GpuSelectionMode::Integrated:
        return "integrated";
    case GpuSelectionMode::Auto:
    default:
        return "auto";
    }
}

inline auto matchesSelectionMode(const vk::PhysicalDeviceProperties &properties, GpuSelectionMode mode) -> bool
{
    switch (mode) {
    case GpuSelectionMode::Dedicated:
        return properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
    case GpuSelectionMode::Integrated:
        return properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu;
    case GpuSelectionMode::Auto:
    default:
        return true;
    }
}

// The extension exposes two independent bits and a device may support
// only computeDerivativeGroupLinear. Requesting the quads bit on such a
// device makes vkCreateDevice fail with VK_ERROR_FEATURE_NOT_PRESENT -
// the engine does not start at all, rather than degrading.
constexpr bool shouldEnableComputeDerivativeGroupQuads(bool extensionPresent, vk::Bool32 quadsSupported)
{
    return extensionPresent && quadsSupported == VK_TRUE;
}

// VUID-VkSamplerCreateInfo-anisotropyEnable-01071: maxAnisotropy must not
// exceed the device's maxSamplerAnisotropy limit when anisotropy is enabled.
// 16x is the ceiling we ask for rather than the device limit itself -
// requesting the raw limit would silently change texture quality per GPU,
// while diminishing returns above 16x make that not worth it.
constexpr float resolveMaxAnisotropy(bool anisotropyEnabled, float deviceLimit)
{
    return anisotropyEnabled ? std::clamp(16.0F, 1.0F, deviceLimit) : 1.0F;
}

inline auto scorePhysicalDevice(const vk::PhysicalDeviceProperties &properties) -> PhysicalDeviceScore
{
    int type_rank = 0;

    switch (properties.deviceType) {
    case vk::PhysicalDeviceType::eDiscreteGpu:
        type_rank = 4;
        break;
    case vk::PhysicalDeviceType::eIntegratedGpu:
        type_rank = 3;
        break;
    case vk::PhysicalDeviceType::eVirtualGpu:
        type_rank = 2;
        break;
    case vk::PhysicalDeviceType::eCpu:
        type_rank = 1;
        break;
    default:
        type_rank = 0;
        break;
    }

    return PhysicalDeviceScore{ type_rank, properties.limits.maxImageDimension2D };
}

}// namespace Kataglyphis
