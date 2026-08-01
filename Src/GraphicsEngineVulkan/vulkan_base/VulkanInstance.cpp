module;

#include "GLFW/glfw3.h"
#include "spdlog/spdlog.h"

#include "common/Utilities.hpp"
#include <cstdint>
#include <cstring>
#include <vector>
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

module kataglyphis.vulkan.instance;

import kataglyphis.vulkan.config;
import kataglyphis.vulkan.debug;

Kataglyphis::VulkanInstance::VulkanInstance()
{
    static vk::detail::DynamicLoader dl;
    VULKAN_HPP_DEFAULT_DISPATCHER.init(dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));

    if (Kataglyphis::ENABLE_VALIDATION_LAYERS && !check_validation_layer_support()) {
        spdlog::error("Validation layers requested, but not available!");
    }

    // info about app
    // most data doesn't affect program; is for developer convenience
    vk::ApplicationInfo app_info{};
    app_info.pApplicationName = "\\__/ Epic Graphics from hell \\__/";// custom name of app
    app_info.applicationVersion = VK_MAKE_VERSION(Kataglyphis::RendererConfig::projectVersionMajor,
      Kataglyphis::RendererConfig::projectVersionMinor,
      Kataglyphis::RendererConfig::projectVersionPatch);// custom version of app
    app_info.pEngineName = "Cataglyphis Renderer";// custom engine name
    app_info.engineVersion = VK_MAKE_VERSION(Kataglyphis::RendererConfig::projectVersionMajor,
      Kataglyphis::RendererConfig::projectVersionMinor,
      Kataglyphis::RendererConfig::projectVersionPatch);// custom engine version
    app_info.apiVersion = Kataglyphis::RendererConfig::vulkanApiVersion;// the vulkan version

    // creation info for a vk::Instance
    vk::InstanceCreateInfo create_info{};
    create_info.pApplicationInfo = &app_info;

    // add validation layers IF enabled to the creeate info struct
    if (Kataglyphis::ENABLE_VALIDATION_LAYERS) {
        create_info.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        create_info.ppEnabledLayerNames = validationLayers.data();

    } else {
        create_info.enabledLayerCount = 0;
        create_info.pNext = nullptr;
    }

    // create list to hold instance extensions
    std::vector<const char *> instance_extensions = std::vector<const char *>();

    // Setup extensions the instance will use
    uint32_t glfw_extensions_count = 0;// GLFW may require multiple extensions
    const char **glfw_extensions = nullptr;// Extensions passed as array of cstrings, so
                                           // need pointer(array) to pointer

    // set GLFW extensions
    glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extensions_count);

    // Add GLFW extensions to list of extensions
    for (size_t i = 0; i < glfw_extensions_count; i++) { instance_extensions.push_back(glfw_extensions[i]); }

    if (Kataglyphis::ENABLE_VALIDATION_LAYERS) { instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); }

    // check instance extensions supported
    if (!check_instance_extension_support(&instance_extensions)) {
        spdlog::critical("VkInstance does not support required extensions!");
        std::abort();
    }

    create_info.enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size());
    create_info.ppEnabledExtensionNames = instance_extensions.data();

    // create instance
    vk::ResultValue<vk::Instance> instance_result = vk::createInstance(create_info);
    ASSERT_VULKAN(instance_result.result, "Failed to create vulkan instance!")
    instance = instance_result.value;

    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
}

auto Kataglyphis::VulkanInstance::check_validation_layer_support() -> bool
{
    std::vector<vk::LayerProperties> availableLayers = vk::enumerateInstanceLayerProperties().value;

    for (const char *layerName : validationLayers) {
        bool layerFound = false;

        for (const auto &layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) { return false; }
    }

    return true;
}

auto Kataglyphis::VulkanInstance::check_instance_extension_support(std::vector<const char *> *check_extensions) -> bool
{
    // create a list of vk::ExtensionProperties
    std::vector<vk::ExtensionProperties> extensions = vk::enumerateInstanceExtensionProperties().value;

    // check if given extensions are in list of available extensions
    for (const auto &check_extension : *check_extensions) {
        bool has_extension = false;

        for (const auto &extension : extensions) {
            if (strcmp(check_extension, extension.extensionName) == 0) {
                has_extension = true;
                break;
            }
        }

        if (!has_extension) {
            spdlog::critical("Required instance extension not supported: {}", check_extension);
            return false;
        }
    }

    return true;
}

void Kataglyphis::VulkanInstance::cleanUp() { instance.destroy(); }

Kataglyphis::VulkanInstance::~VulkanInstance() = default;
