module;

#include <cstdint>
#include <cstring>

#include "common/Utilities.hpp"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <set>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

module kataglyphis.vulkan.device;

namespace {
constexpr int DEVICE_TYPE_SCORE_DISCRETE = 10000;
constexpr int DEVICE_TYPE_SCORE_INTEGRATED = 1000;
constexpr int DEVICE_TYPE_SCORE_VIRTUAL = 100;
constexpr int DEVICE_TYPE_SCORE_CPU = 10;

enum class GpuSelectionMode : std::uint8_t { Auto, Dedicated, Integrated };

auto readGpuSelectionFromEnvironment() -> std::string
{
#if defined(_WIN32)
    std::size_t required_size = 0;
    if (getenv_s(&required_size, nullptr, 0, "KATAGLYPHIS_VK_GPU") != 0 || required_size == 0) { return ""; }

    std::string value(required_size, '\0');
    if (getenv_s(&required_size, value.data(), value.size(), "KATAGLYPHIS_VK_GPU") != 0 || required_size == 0) {
        return "";
    }

    value.resize(required_size - 1);
    return value;
#else
    const char *value = std::getenv("KATAGLYPHIS_VK_GPU");
    if (value == nullptr) { return ""; }
    return std::string(value);
#endif
}

auto parseGpuSelectionMode() -> GpuSelectionMode
{
    std::string mode = readGpuSelectionFromEnvironment();
    if (mode.empty()) { return GpuSelectionMode::Auto; }

    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (mode == "dedicated") { return GpuSelectionMode::Dedicated; }
    if (mode == "integrated") { return GpuSelectionMode::Integrated; }

    return GpuSelectionMode::Auto;
}

auto gpuSelectionModeToString(GpuSelectionMode mode) -> const char *
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

auto matchesSelectionMode(const VkPhysicalDeviceProperties &properties, GpuSelectionMode mode) -> bool
{
    switch (mode) {
    case GpuSelectionMode::Dedicated:
        return properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    case GpuSelectionMode::Integrated:
        return properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    case GpuSelectionMode::Auto:
    default:
        return true;
    }
}

auto scorePhysicalDevice(const VkPhysicalDeviceProperties &properties) -> int
{
    int score = 0;

    switch (properties.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        score += DEVICE_TYPE_SCORE_DISCRETE;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        score += DEVICE_TYPE_SCORE_INTEGRATED;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        score += DEVICE_TYPE_SCORE_VIRTUAL;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        score += DEVICE_TYPE_SCORE_CPU;
        break;
    default:
        break;
    }

    score += static_cast<int>(properties.limits.maxImageDimension2D);
    return score;
}

auto deviceTypeToString(VkPhysicalDeviceType type) -> const char *
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "Discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "Integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "Virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "CPU";
    default:
        return "Other";
    }
}
}// namespace

Kataglyphis::VulkanDevice::VulkanDevice(VulkanInstance *instance, VkSurfaceKHR *surface)
  : instance(instance), surface(surface)
{


    get_physical_device();
    create_logical_device();
}

auto Kataglyphis::VulkanDevice::getSwapchainDetails() -> Kataglyphis::VulkanRendererInternals::SwapChainDetails
{
    return getSwapchainDetails(physical_device);
}

void Kataglyphis::VulkanDevice::cleanUp() { vkDestroyDevice(logical_device, nullptr); }

Kataglyphis::VulkanDevice::~VulkanDevice() = default;

auto Kataglyphis::VulkanDevice::getQueueFamilies() -> Kataglyphis::VulkanRendererInternals::QueueFamilyIndices
{
    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices indices{};

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_family_list(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_family_list.data());

    // Go through each queue family and check if it has at least 1 of required
    // types we need to keep track th eindex by our own
    uint32_t index = 0;
    for (const auto &queue_family : queue_family_list) {
        // first check if queue family has at least 1 queue in that family
        // Queue can be multiple types defined through bitfield. Need to bitwise AND
        // with VK_QUE_*_BIT to check if has required  type
        if (queue_family.queueCount > 0 && ((queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u)) {
            indices.graphics_family = static_cast<int>(index);// if queue family valid, than get index
        }

        if (queue_family.queueCount > 0 && ((queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u)) {
            indices.compute_family = static_cast<int>(index);
        }

        // check if queue family suppports presentation
        VkBool32 presentation_support = 0u;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, index, *surface, &presentation_support);
        // check if queue is presentation type (can be both graphics and
        // presentation)
        if (queue_family.queueCount > 0 && (presentation_support != 0u)) {
            indices.presentation_family = static_cast<int>(index);
        }

        // check if queue family indices are in a valid state
        if (indices.is_valid()) { break; }

        index++;
    }

    return indices;
}

void Kataglyphis::VulkanDevice::get_physical_device()
{
    const GpuSelectionMode selection_mode = parseGpuSelectionMode();

    // Enumerate physical devices the vkInstance can access
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance->getVulkanInstance(), &device_count, nullptr);

    // if no devices available, then none support of Vulkan
    if (device_count == 0) { spdlog::error("Can not find GPU's that support Vulkan Instance!"); }

    // Get list of physical devices
    std::vector<VkPhysicalDevice> device_list(device_count);
    vkEnumeratePhysicalDevices(instance->getVulkanInstance(), &device_count, device_list.data());

    int best_device_score = std::numeric_limits<int>::min();
    int best_device_score_fallback = std::numeric_limits<int>::min();
    VkPhysicalDevice fallback_device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties fallback_properties{};

    for (const auto &device : device_list) {
        if (!check_device_suitable(device)) { continue; }

        VkPhysicalDeviceProperties candidate_properties;
        vkGetPhysicalDeviceProperties(device, &candidate_properties);

        const int candidate_score = scorePhysicalDevice(candidate_properties);
        if (candidate_score > best_device_score_fallback) {
            best_device_score_fallback = candidate_score;
            fallback_device = device;
            fallback_properties = candidate_properties;
        }

        if (!matchesSelectionMode(candidate_properties, selection_mode)) { continue; }

        if (candidate_score > best_device_score) {
            best_device_score = candidate_score;
            physical_device = device;
            device_properties = candidate_properties;
        }
    }

    if (physical_device == VK_NULL_HANDLE && fallback_device != VK_NULL_HANDLE) {
        physical_device = fallback_device;
        device_properties = fallback_properties;
        spdlog::default_logger_raw()->log(spdlog::level::warn,
          std::string("No suitable Vulkan GPU matching selection mode '") + gpuSelectionModeToString(selection_mode)
            + "' found. Falling back to auto device selection.");
    }

    if (physical_device == VK_NULL_HANDLE) {
        spdlog::critical("Failed to find a suitable Vulkan physical device.");
        std::abort();
    }

    // get properties of our new device
    vkGetPhysicalDeviceProperties(physical_device, &device_properties);
    spdlog::default_logger_raw()->log(spdlog::level::info,
      std::string("Selected Vulkan physical device: ") + device_properties.deviceName + " ("
        + deviceTypeToString(device_properties.deviceType) + ")");
    spdlog::default_logger_raw()->log(
      spdlog::level::info, std::string("Vulkan GPU selection mode: ") + gpuSelectionModeToString(selection_mode));
}

void Kataglyphis::VulkanDevice::create_logical_device()
{
    // get the queue family indices for the chosen physical device
    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices const indices = getQueueFamilies();

    // vector for queue creation information and set for family indices
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    std::set<int> const queue_family_indices = {
        indices.graphics_family, indices.presentation_family, indices.compute_family
    };
    float const queue_priority = 1.0F;

    // Queue the logical device needs to create and info to do so (only 1 for now,
    // will add more later!)
    for (int const queue_family_index : queue_family_indices) {
        VkDeviceQueueCreateInfo queue_create_info{};
        queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_info.queueFamilyIndex =
          static_cast<uint32_t>(queue_family_index);// the index of the family to create a queue from
        queue_create_info.queueCount = 1;// number of queues to create
        queue_create_info.pQueuePriorities = &queue_priority;// Vulkan needs to know how to handle multiple queues, so
                                                       // decide priority (1 = highest)

        queue_create_infos.push_back(queue_create_info);
    }

    VkPhysicalDeviceVulkan13Features available_features13{};
    available_features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    available_features13.pNext = nullptr;

    VkPhysicalDeviceVulkan12Features available_features12{};
    available_features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    available_features12.pNext = &available_features13;

    VkPhysicalDeviceFeatures2 available_features2{};
    available_features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    available_features2.pNext = &available_features12;
    vkGetPhysicalDeviceFeatures2(physical_device, &available_features2);

    // --ENABLE RAY TRACING PIPELINE
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline_features{};
    ray_tracing_pipeline_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    ray_tracing_pipeline_features.pNext = nullptr;
    ray_tracing_pipeline_features.rayTracingPipeline = VK_FALSE;

    // -- ENABLE ACCELERATION STRUCTURES
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features{};
    acceleration_structure_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    acceleration_structure_features.pNext = &ray_tracing_pipeline_features;
    acceleration_structure_features.accelerationStructure = VK_FALSE;
    acceleration_structure_features.accelerationStructureCaptureReplay = VK_FALSE;
    acceleration_structure_features.accelerationStructureIndirectBuild = VK_FALSE;
    acceleration_structure_features.accelerationStructureHostCommands = VK_FALSE;
    acceleration_structure_features.descriptorBindingAccelerationStructureUpdateAfterBind = VK_FALSE;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.maintenance4 = VK_FALSE;
    features13.robustImageAccess = VK_FALSE;
    features13.inlineUniformBlock = VK_FALSE;
    features13.descriptorBindingInlineUniformBlockUpdateAfterBind = VK_FALSE;
    features13.pipelineCreationCacheControl = VK_FALSE;
    features13.privateData = VK_FALSE;
    features13.shaderDemoteToHelperInvocation = VK_FALSE;
    features13.shaderTerminateInvocation = VK_FALSE;
    features13.subgroupSizeControl = VK_FALSE;
    features13.computeFullSubgroups = VK_FALSE;
    features13.synchronization2 = VK_FALSE;
    features13.textureCompressionASTC_HDR = VK_FALSE;
    features13.shaderZeroInitializeWorkgroupMemory = VK_FALSE;
    features13.dynamicRendering = VK_FALSE;
    features13.shaderIntegerDotProduct = VK_FALSE;
    features13.pNext = &acceleration_structure_features;

    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeature{};
    rayQueryFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    rayQueryFeature.pNext = &features13;
    rayQueryFeature.rayQuery = VK_FALSE;

    VkPhysicalDeviceRayQueryFeaturesKHR availableRayQueryFeature{};
    availableRayQueryFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR availableRayTracingPipelineFeatures{};
    availableRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    availableRayTracingPipelineFeatures.pNext = &availableRayQueryFeature;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR availableAccelerationStructureFeatures{};
    availableAccelerationStructureFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    availableAccelerationStructureFeatures.pNext = &availableRayTracingPipelineFeatures;

    VkPhysicalDeviceFeatures2 availableRayTracingFeatures2{};
    availableRayTracingFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    availableRayTracingFeatures2.pNext = &availableAccelerationStructureFeatures;
    vkGetPhysicalDeviceFeatures2(physical_device, &availableRayTracingFeatures2);

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.pNext = nullptr;
    features12.bufferDeviceAddress = available_features12.bufferDeviceAddress;
    features12.scalarBlockLayout = available_features12.scalarBlockLayout;
    features12.descriptorIndexing = available_features12.descriptorIndexing;
    features12.runtimeDescriptorArray = available_features12.runtimeDescriptorArray;
    features12.shaderSampledImageArrayNonUniformIndexing =
      available_features12.shaderSampledImageArrayNonUniformIndexing;

    VkPhysicalDeviceFeatures2 features2{};
    features2.pNext = nullptr;
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.features.samplerAnisotropy = VK_TRUE;
    features2.features.shaderInt64 = VK_TRUE;
    features2.features.geometryShader = VK_TRUE;
    features2.features.fragmentStoresAndAtomics = VK_TRUE;
    features2.features.logicOp = VK_TRUE;
    features2.features.robustBufferAccess = available_features2.features.robustBufferAccess;

    // -- PREPARE FOR HAVING MORE EXTENSION BECAUSE WE NEED RAYTRACING
    // CAPABILITIES
    std::vector<const char *> extensions(device_extensions);

    // Query available extensions for the physical device
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extensionCount, availableExtensions.data());

    // Helper function to check if an extension is supported
    auto isExtensionSupported = [&availableExtensions](const char *extensionName) -> bool {
        for (const auto &ext : availableExtensions) {
            if (strcmp(ext.extensionName, extensionName) == 0) { return true; }
        }
        return false;
    };

    const bool hasBufferDeviceAddressFeature = available_features12.bufferDeviceAddress == VK_TRUE;
    deviceSupportsBufferDeviceAddress = hasBufferDeviceAddressFeature;
    const bool hasRequiredDescriptorIndexingFeatures =
      available_features12.descriptorIndexing == VK_TRUE && available_features12.runtimeDescriptorArray == VK_TRUE
      && available_features12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;

    spdlog::default_logger_raw()->log(spdlog::level::info,
      std::string("Feature support: bufferDeviceAddress=") + (hasBufferDeviceAddressFeature ? "true" : "false")
        + ", descriptorIndexing=" + (available_features12.descriptorIndexing == VK_TRUE ? "true" : "false")
        + ", runtimeDescriptorArray=" + (available_features12.runtimeDescriptorArray == VK_TRUE ? "true" : "false")
        + ", sampledImageArrayNonUniformIndexing="
        + (available_features12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE ? "true" : "false")
        + ", robustBufferAccess=" + (available_features2.features.robustBufferAccess == VK_TRUE ? "true" : "false"));

    for (const char *extensionName : device_extensions_for_raytracing) {
        if (!isExtensionSupported(extensionName)) {
            deviceSupportsHardwareAcceleratedRRT = false;
            spdlog::default_logger_raw()->log(
              spdlog::level::info, std::string("Required extension not supported: ") + extensionName);
        }
    }

    if (deviceSupportsHardwareAcceleratedRRT && !hasRequiredDescriptorIndexingFeatures) {
        deviceSupportsHardwareAcceleratedRRT = false;
        spdlog::info(
          "Required Vulkan 1.2 descriptor indexing features are not supported; disabling hardware ray tracing path.");
    }

    if (deviceSupportsHardwareAcceleratedRRT && !hasBufferDeviceAddressFeature) {
        deviceSupportsHardwareAcceleratedRRT = false;
        spdlog::info("bufferDeviceAddress feature is not supported; disabling hardware ray tracing path.");
    }

    const bool hasMaintenance4Feature = available_features13.maintenance4 == VK_TRUE;

    const bool hasRequiredRayTracingFeatures = availableAccelerationStructureFeatures.accelerationStructure == VK_TRUE
                                               && availableRayTracingPipelineFeatures.rayTracingPipeline == VK_TRUE
                                               && availableRayQueryFeature.rayQuery == VK_TRUE;

    if (deviceSupportsHardwareAcceleratedRRT && !hasRequiredRayTracingFeatures) {
        deviceSupportsHardwareAcceleratedRRT = false;
        spdlog::default_logger_raw()->log(spdlog::level::info,
          std::string("Required ray tracing features are not fully supported (accelerationStructure=")
            + (availableAccelerationStructureFeatures.accelerationStructure == VK_TRUE ? "true" : "false")
            + ", rayTracingPipeline="
            + (availableRayTracingPipelineFeatures.rayTracingPipeline == VK_TRUE ? "true" : "false")
            + ", rayQuery=" + (availableRayQueryFeature.rayQuery == VK_TRUE ? "true" : "false")
            + "); disabling hardware ray tracing path.");
    }

    if (deviceSupportsHardwareAcceleratedRRT && !hasMaintenance4Feature) {
        deviceSupportsHardwareAcceleratedRRT = false;
        spdlog::default_logger_raw()->log(spdlog::level::info,
          "Vulkan 1.3 maintenance4 feature is not supported; disabling hardware ray tracing/path tracing path.");
    }

    if (deviceSupportsHardwareAcceleratedRRT) {
        // COPY ALL NECESSARY EXTENSIONS FOR RAYTRACING TO THE EXTENSION
        extensions.insert(
          extensions.begin(), device_extensions_for_raytracing.begin(), device_extensions_for_raytracing.end());

        features12.bufferDeviceAddress = VK_TRUE;
        features12.descriptorIndexing = VK_TRUE;
        features12.runtimeDescriptorArray = VK_TRUE;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        features13.maintenance4 = VK_TRUE;

        acceleration_structure_features.accelerationStructure = VK_TRUE;
        acceleration_structure_features.accelerationStructureCaptureReplay = VK_FALSE;
        ray_tracing_pipeline_features.rayTracingPipeline = VK_TRUE;
        rayQueryFeature.rayQuery = VK_TRUE;
        features12.pNext = &rayQueryFeature;
    }

    if (!deviceSupportsHardwareAcceleratedRRT && !hasBufferDeviceAddressFeature) {
        spdlog::info("bufferDeviceAddress feature is not supported; related shader capabilities may be unavailable.");
    }

    if (features2.features.robustBufferAccess == VK_TRUE) {
        spdlog::info("Enabling robustBufferAccess for additional GPU memory access safety.");
    } else {
        spdlog::info("robustBufferAccess is not supported on this device.");
    }

    features2.pNext = &features12;

    // information to create logical device (sometimes called "device")
    VkDeviceCreateInfo device_create_info{};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount =
      static_cast<uint32_t>(queue_create_infos.size());// number of queue create infos
    device_create_info.pQueueCreateInfos = queue_create_infos.data();// list of queue create infos so device can
                                                                     // create required queues
    device_create_info.enabledExtensionCount =
      static_cast<uint32_t>(extensions.size());// number of enabled logical device extensions
    device_create_info.ppEnabledExtensionNames = extensions.data();// list of enabled logical device extensions
    device_create_info.flags = 0;
    device_create_info.pEnabledFeatures = nullptr;
    device_create_info.pNext = &features2;

    // create logical device for the given physical device
    VkResult const result = vkCreateDevice(physical_device, &device_create_info, nullptr, &logical_device);
    ASSERT_VULKAN(result, "Failed to create a logical device!");
    if (result != VK_SUCCESS || logical_device == VK_NULL_HANDLE) {
        spdlog::critical("Unable to continue without a valid Vulkan logical device.");
        std::abort();
    }

    //  Queues are created at the same time as the device...
    // So we want handle to queues
    // From given logical device of given queue family, of given queue index (0
    // since only one queue), place reference in given VkQueue
    vkGetDeviceQueue(logical_device, static_cast<uint32_t>(indices.graphics_family), 0, &graphics_queue);
    vkGetDeviceQueue(logical_device, static_cast<uint32_t>(indices.presentation_family), 0, &presentation_queue);
    vkGetDeviceQueue(logical_device, static_cast<uint32_t>(indices.compute_family), 0, &compute_queue);
}

auto Kataglyphis::VulkanDevice::getQueueFamilies(VkPhysicalDevice selectedPhysicalDevice)
  -> Kataglyphis::VulkanRendererInternals::QueueFamilyIndices
{
    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices indices{};

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(selectedPhysicalDevice, &queue_family_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_family_list(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(selectedPhysicalDevice, &queue_family_count, queue_family_list.data());

    // Go through each queue family and check if it has at least 1 of required
    // types we need to keep track th eindex by our own
    uint32_t index = 0;
    for (const auto &queue_family : queue_family_list) {
        // first check if queue family has at least 1 queue in that family
        // Queue can be multiple types defined through bitfield. Need to bitwise AND
        // with VK_QUE_*_BIT to check if has required  type
        if (queue_family.queueCount > 0 && ((queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u)) {
            indices.graphics_family = static_cast<int>(index);// if queue family valid, than get index
        }

        if (queue_family.queueCount > 0 && ((queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u)) {
            indices.compute_family = static_cast<int>(index);
        }

        // check if queue family suppports presentation
        VkBool32 presentation_support = 0u;
        vkGetPhysicalDeviceSurfaceSupportKHR(selectedPhysicalDevice, index, *surface, &presentation_support);
        // check if queue is presentation type (can be both graphics and
        // presentation)
        if (queue_family.queueCount > 0 && (presentation_support != 0u)) {
            indices.presentation_family = static_cast<int>(index);
        }

        // check if queue family indices are in a valid state
        if (indices.is_valid()) { break; }

        index++;
    }

    return indices;
}

auto Kataglyphis::VulkanDevice::getSwapchainDetails(VkPhysicalDevice device)
  -> Kataglyphis::VulkanRendererInternals::SwapChainDetails
{
    Kataglyphis::VulkanRendererInternals::SwapChainDetails swapchain_details{};
    // get the surface capabilities for the given surface on the given physical
    // device
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, *surface, &swapchain_details.surface_capabilities);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, *surface, &format_count, nullptr);

    // if formats returned, get list of formats
    if (format_count != 0) {
        swapchain_details.formats.resize(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, *surface, &format_count, swapchain_details.formats.data());
    }

    uint32_t presentation_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, *surface, &presentation_count, nullptr);

    // if presentation modes returned, get list of presentation modes
    if (presentation_count > 0) {
        swapchain_details.presentation_mode.resize(presentation_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(
          device, *surface, &presentation_count, swapchain_details.presentation_mode.data());
    }

    return swapchain_details;
}

auto Kataglyphis::VulkanDevice::check_device_suitable(VkPhysicalDevice device) -> bool
{
    VkPhysicalDeviceFeatures device_features;
    vkGetPhysicalDeviceFeatures(device, &device_features);

    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices indices = getQueueFamilies(device);

    bool const extensions_supported = check_device_extension_support(device);

    bool swap_chain_valid = false;

    if (extensions_supported) {
        Kataglyphis::VulkanRendererInternals::SwapChainDetails const swap_chain_details = getSwapchainDetails(device);
        swap_chain_valid = !swap_chain_details.presentation_mode.empty() && !swap_chain_details.formats.empty();
    }

    return indices.is_valid() && extensions_supported && swap_chain_valid && (device_features.samplerAnisotropy != 0u);
}

auto Kataglyphis::VulkanDevice::check_device_extension_support(VkPhysicalDevice device) -> bool
{
    uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);

    if (extension_count == 0) { return false; }

    // populate list of extensions
    std::vector<VkExtensionProperties> extensions(extension_count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, extensions.data());

    for (const auto &device_extension : device_extensions) {
        bool has_extension = false;

        for (const auto &extension : extensions) {
            if (strcmp(device_extension, extension.extensionName) == 0) {
                has_extension = true;
                break;
            }
        }

        if (!has_extension) { return false; }
    }

    return true;
}
