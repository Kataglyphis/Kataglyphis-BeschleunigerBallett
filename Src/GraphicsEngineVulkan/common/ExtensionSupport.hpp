#pragma once

#include <span>
#include <string_view>
#include <vulkan/vulkan.hpp>

namespace Kataglyphis {

[[nodiscard]] inline bool supportsExtension(std::span<const vk::ExtensionProperties> available, std::string_view name)
{
    for (const auto &extension : available) {
        if (std::string_view(extension.extensionName.data()) == name) { return true; }
    }
    return false;
}

[[nodiscard]] inline bool supportsLayer(std::span<const vk::LayerProperties> available, std::string_view name)
{
    for (const auto &layer : available) {
        if (std::string_view(layer.layerName.data()) == name) { return true; }
    }
    return false;
}

// nullptr when every required name is present; otherwise the first missing one.
[[nodiscard]] inline const char *firstMissingExtension(std::span<const vk::ExtensionProperties> available,
  std::span<const char *const> required)
{
    for (const char *name : required) {
        if (!supportsExtension(available, name)) { return name; }
    }
    return nullptr;
}

}// namespace Kataglyphis
