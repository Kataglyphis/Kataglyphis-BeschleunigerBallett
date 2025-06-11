#include "vulkan_base/VulkanInstance.hpp"
#include "vulkan_base/VulkanDebug.hpp"

#include "common/Utilities.hpp"
#include "vulkan_base/VulkanDebug.hpp"
#include <string.h>
#include <string>

Kataglyphis::VulkanInstance::VulkanInstance()
{
    if (Kataglyphis::ENABLE_VALIDATION_LAYERS && !check_validation_layer_support()) {
        spdlog::error("Validation layers requested, but not available!");
    }

    // info about app
    // most data doesn't affect program; is for developer convenience
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "\\__/ Epic Graphics from hell \\__/";// custom name of app
    app_info.applicationVersion = VK_MAKE_VERSION(1, 3, 1);// custom version of app
    app_info.pEngineName = "Cataglyphis Renderer";// custom engine name
    app_info.engineVersion = VK_MAKE_VERSION(1, 3, 3);// custom engine version
    app_info.apiVersion = VK_API_VERSION_1_3;// the vulkan version

    // creation info for a VkInstance
    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
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
    const char **glfw_extensions;// Extensions passed as array of cstrings, so
                                 // need pointer(array) to pointer

    // set GLFW extensions
    glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extensions_count);

    // Add GLFW extensions to list of extensions
    for (size_t i = 0; i < glfw_extensions_count; i++) { instance_extensions.push_back(glfw_extensions[i]); }

    if (Kataglyphis::ENABLE_VALIDATION_LAYERS) { instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); }

    // check instance extensions supported
    if (!check_instance_extension_support(&instance_extensions)) {
        spdlog::error("VkInstance does not support required extensions!");
    }

    create_info.enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size());
    create_info.ppEnabledExtensionNames = instance_extensions.data();

    // create instance
    VkResult result = vkCreateInstance(&create_info, nullptr, &instance);
    ASSERT_VULKAN(result, "Failed to create a Vulkan instance!");
}

bool Kataglyphis::VulkanInstance::check_validation_layer_support()
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

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

bool Kataglyphis::VulkanInstance::check_instance_extension_support(std::vector<const char *> *check_extensions)
{
    // Need to get number of extensions to create array of correct size to hold
    // extensions
    uint32_t extension_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);

    // create a list of VkExtensionProperties using count
    std::vector<VkExtensionProperties> extensions(extension_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data());

    // check if given extensions are in list of available extensions
    for (const auto &check_extension : *check_extensions) {
        bool has_extension = false;

        for (const auto &extension : extensions) {
            if (strcmp(check_extension, extension.extensionName)) {
                has_extension = true;
                break;
            }
        }

        if (!has_extension) { return false; }
    }

    return true;
}

void Kataglyphis::VulkanInstance::cleanUp() { vkDestroyInstance(instance, nullptr); }

Kataglyphis::VulkanInstance::~VulkanInstance() {}
