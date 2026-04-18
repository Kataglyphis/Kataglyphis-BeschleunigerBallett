module;

#include "renderer/SwapChainDetails.hpp"
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
#include <vulkan/vulkan.hpp>

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

auto matchesSelectionMode(const vk::PhysicalDeviceProperties &properties, GpuSelectionMode mode) -> bool
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

auto scorePhysicalDevice(const vk::PhysicalDeviceProperties &properties) -> int
{
    int score = 0;

    switch (properties.deviceType) {
    case vk::PhysicalDeviceType::eDiscreteGpu:
        score += DEVICE_TYPE_SCORE_DISCRETE;
        break;
    case vk::PhysicalDeviceType::eIntegratedGpu:
        score += DEVICE_TYPE_SCORE_INTEGRATED;
        break;
    case vk::PhysicalDeviceType::eVirtualGpu:
        score += DEVICE_TYPE_SCORE_VIRTUAL;
        break;
    case vk::PhysicalDeviceType::eCpu:
        score += DEVICE_TYPE_SCORE_CPU;
        break;
    default:
        break;
    }

    score += static_cast<int>(properties.limits.maxImageDimension2D);
    return score;
}

auto deviceTypeToString(vk::PhysicalDeviceType type) -> const char *
{
    switch (type) {
    case vk::PhysicalDeviceType::eDiscreteGpu:
        return "Discrete GPU";
    case vk::PhysicalDeviceType::eIntegratedGpu:
        return "Integrated GPU";
    case vk::PhysicalDeviceType::eVirtualGpu:
        return "Virtual GPU";
    case vk::PhysicalDeviceType::eCpu:
        return "CPU";
    default:
        return "Other";
    }
}
}// namespace

Kataglyphis::VulkanDevice::VulkanDevice(VulkanInstance *instance, vk::SurfaceKHR *surface)
  : instance(instance), surface(surface)
{


    get_physical_device();
    create_logical_device();
}

auto Kataglyphis::VulkanDevice::getSwapchainDetails() -> Kataglyphis::VulkanRendererInternals::SwapChainDetails
{
    return getSwapchainDetails(physical_device);
}

void Kataglyphis::VulkanDevice::cleanUp() { logical_device.destroy(); }

Kataglyphis::VulkanDevice::~VulkanDevice() = default;

auto Kataglyphis::VulkanDevice::getQueueFamilies() -> Kataglyphis::VulkanRendererInternals::QueueFamilyIndices
{
    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices indices{};

    std::vector<vk::QueueFamilyProperties> queue_family_list = physical_device.getQueueFamilyProperties();

    // Go through each queue family and check if it has at least 1 of required
    // types we need to keep track th eindex by our own
    uint32_t index = 0;
    for (const auto &queue_family : queue_family_list) {
        // first check if queue family has at least 1 queue in that family
        // Queue can be multiple types defined through bitfield. Need to bitwise AND
        // with vk::QueueFlagBits to check if has required  type
        if (queue_family.queueCount > 0 && (queue_family.queueFlags & vk::QueueFlagBits::eGraphics)) {
            indices.graphics_family = static_cast<int>(index);// if queue family valid, than get index
        }

        if (queue_family.queueCount > 0 && (queue_family.queueFlags & vk::QueueFlagBits::eCompute)) {
            indices.compute_family = static_cast<int>(index);
        }

        // check if queue family suppports presentation
        vk::Bool32 presentation_support = physical_device.getSurfaceSupportKHR(index, *surface).value;
        // check if queue is presentation type (can be both graphics and
        // presentation)
        if (queue_family.queueCount > 0 && presentation_support) {
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
    std::vector<vk::PhysicalDevice> device_list = instance->getVulkanInstance().enumeratePhysicalDevices().value;

    // if no devices available, then none support of Vulkan
    if (device_list.empty()) { spdlog::error("Can not find GPU's that support Vulkan Instance!"); }

    int best_device_score = std::numeric_limits<int>::min();
    int best_device_score_fallback = std::numeric_limits<int>::min();
    vk::PhysicalDevice fallback_device{};
    vk::PhysicalDeviceProperties fallback_properties{};

    for (const auto &device : device_list) {
        if (!check_device_suitable(device)) { continue; }

        vk::PhysicalDeviceProperties candidate_properties = device.getProperties();

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

    if (!physical_device && fallback_device) {
        physical_device = fallback_device;
        device_properties = fallback_properties;
        spdlog::default_logger_raw()->log(spdlog::level::warn,
          std::string("No suitable Vulkan GPU matching selection mode '") + gpuSelectionModeToString(selection_mode)
            + "' found. Falling back to auto device selection.");
    }

    if (!physical_device) {
        spdlog::critical("Failed to find a suitable Vulkan physical device.");
        std::abort();
    }

    // get properties of our new device
    device_properties = physical_device.getProperties();
    spdlog::default_logger_raw()->log(spdlog::level::info,
      std::string("Selected Vulkan physical device: ") + device_properties.deviceName.data() + " ("
        + deviceTypeToString(device_properties.deviceType) + ")");
    spdlog::default_logger_raw()->log(
      spdlog::level::info, std::string("Vulkan GPU selection mode: ") + gpuSelectionModeToString(selection_mode));
}

void Kataglyphis::VulkanDevice::create_logical_device()
{
    // get the queue family indices for the chosen physical device
    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices const indices = getQueueFamilies();

    // vector for queue creation information and set for family indices
    std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
    std::set<int> const queue_family_indices = {
        indices.graphics_family, indices.presentation_family, indices.compute_family
    };
    std::vector<float> queue_priorities(queue_family_indices.size(), 1.0F);
    queue_create_infos.reserve(queue_family_indices.size());

    // Queue the logical device needs to create and info to do so (only 1 for now,
    // will add more later!)
    std::size_t priority_index = 0;
    for (int const queue_family_index : queue_family_indices) {
        vk::DeviceQueueCreateInfo queue_create_info{};
        queue_create_info.queueFamilyIndex =
          static_cast<uint32_t>(queue_family_index);// the index of the family to create a queue from
        queue_create_info.queueCount = 1;// number of queues to create
        queue_create_info.pQueuePriorities =
          &queue_priorities[priority_index];// Vulkan needs to know how to handle multiple queues, so
                                            // decide priority (1 = highest)

        queue_create_infos.push_back(queue_create_info);
        ++priority_index;
    }

    vk::PhysicalDeviceVulkan13Features available_features13{};
    available_features13.pNext = nullptr;

    vk::PhysicalDeviceVulkan12Features available_features12{};
    available_features12.pNext = &available_features13;

    vk::PhysicalDeviceVulkan11Features available_features11{};
    available_features11.pNext = &available_features12;

    vk::PhysicalDeviceFeatures2 available_features2{};
    available_features2.pNext = &available_features11;
    physical_device.getFeatures2(&available_features2);

    // --ENABLE RAY TRACING PIPELINE
    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline_features{};
    ray_tracing_pipeline_features.pNext = nullptr;
    ray_tracing_pipeline_features.rayTracingPipeline = false;

    // -- ENABLE ACCELERATION STRUCTURES
    vk::PhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features{};
    acceleration_structure_features.pNext = &ray_tracing_pipeline_features;
    acceleration_structure_features.accelerationStructure = false;
    acceleration_structure_features.accelerationStructureCaptureReplay = false;
    acceleration_structure_features.accelerationStructureIndirectBuild = false;
    acceleration_structure_features.accelerationStructureHostCommands = false;
    acceleration_structure_features.descriptorBindingAccelerationStructureUpdateAfterBind = false;

    vk::PhysicalDeviceVulkan13Features features13{};
    features13.maintenance4 = false;
    features13.robustImageAccess = false;
    features13.inlineUniformBlock = false;
    features13.descriptorBindingInlineUniformBlockUpdateAfterBind = false;
    features13.pipelineCreationCacheControl = false;
    features13.privateData = false;
    features13.shaderDemoteToHelperInvocation = available_features13.shaderDemoteToHelperInvocation;
    features13.shaderTerminateInvocation = false;
    features13.subgroupSizeControl = false;
    features13.computeFullSubgroups = false;
    features13.synchronization2 = false;
    features13.textureCompressionASTC_HDR = false;
    features13.shaderZeroInitializeWorkgroupMemory = false;
    features13.dynamicRendering = false;
    features13.shaderIntegerDotProduct = false;
    features13.pNext = &acceleration_structure_features;

    vk::PhysicalDeviceRayQueryFeaturesKHR rayQueryFeature{};
    rayQueryFeature.pNext = &features13;
    rayQueryFeature.rayQuery = false;

    vk::PhysicalDeviceRayQueryFeaturesKHR availableRayQueryFeature{};

    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR availableRayTracingPipelineFeatures{};
    availableRayTracingPipelineFeatures.pNext = &availableRayQueryFeature;

    vk::PhysicalDeviceAccelerationStructureFeaturesKHR availableAccelerationStructureFeatures{};
    availableAccelerationStructureFeatures.pNext = &availableRayTracingPipelineFeatures;

    vk::PhysicalDeviceFeatures2 availableRayTracingFeatures2{};
    availableRayTracingFeatures2.pNext = &availableAccelerationStructureFeatures;
    physical_device.getFeatures2(&availableRayTracingFeatures2);

    vk::PhysicalDeviceVulkan12Features features12{};
    features12.pNext = &features13;
    features12.bufferDeviceAddress = available_features12.bufferDeviceAddress;
    features12.scalarBlockLayout = available_features12.scalarBlockLayout;
    features12.descriptorIndexing = available_features12.descriptorIndexing;
    features12.runtimeDescriptorArray = available_features12.runtimeDescriptorArray;
    features12.shaderSampledImageArrayNonUniformIndexing =
      available_features12.shaderSampledImageArrayNonUniformIndexing;

    vk::PhysicalDeviceVulkan11Features features11{};
    features11.pNext = &features12;
    features11.multiview = available_features11.multiview;

    vk::PhysicalDeviceFeatures2 features2{};
    features2.pNext = nullptr;
    features2.features.samplerAnisotropy = available_features2.features.samplerAnisotropy;
    features2.features.shaderInt64 = available_features2.features.shaderInt64;
    features2.features.geometryShader = available_features2.features.geometryShader;
    features2.features.fragmentStoresAndAtomics = available_features2.features.fragmentStoresAndAtomics;
    features2.features.logicOp = available_features2.features.logicOp;
    features2.features.robustBufferAccess = VK_FALSE;

    // -- PREPARE FOR HAVING MORE EXTENSION BECAUSE WE NEED RAYTRACING
    // CAPABILITIES
    std::vector<const char *> extensions(device_extensions);

    // Query available extensions for the physical device
    std::vector<vk::ExtensionProperties> availableExtensions =
      physical_device.enumerateDeviceExtensionProperties().value;

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

        features12.bufferDeviceAddress = true;
        features12.descriptorIndexing = true;
        features12.runtimeDescriptorArray = true;
        features12.shaderSampledImageArrayNonUniformIndexing = true;
        features13.maintenance4 = true;

        acceleration_structure_features.accelerationStructure = true;
        acceleration_structure_features.accelerationStructureCaptureReplay = false;
        ray_tracing_pipeline_features.rayTracingPipeline = true;
        rayQueryFeature.rayQuery = true;
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

    features2.pNext = &features11;

    // information to create logical device (sometimes called "device")
    vk::DeviceCreateInfo device_create_info{};
    device_create_info.queueCreateInfoCount =
      static_cast<uint32_t>(queue_create_infos.size());// number of queue create infos
    device_create_info.pQueueCreateInfos = queue_create_infos.data();// list of queue create infos so device can
                                                                     // create required queues
    device_create_info.enabledExtensionCount =
      static_cast<uint32_t>(extensions.size());// number of enabled logical device extensions
    device_create_info.ppEnabledExtensionNames = extensions.data();// list of enabled logical device extensions
    device_create_info.flags = {};
    device_create_info.pEnabledFeatures = nullptr;
    device_create_info.pNext = &features2;

    // create logical device for the given physical device
    VkDevice c_device;
    VkResult const result = vkCreateDevice(static_cast<VkPhysicalDevice>(physical_device), reinterpret_cast<const VkDeviceCreateInfo*>(&device_create_info), nullptr, &c_device);
    logical_device = c_device;
    if (result != VK_SUCCESS) {
        spdlog::critical("Failed to create logical device. Vulkan Result: {}", static_cast<int>(result));
    }
    ASSERT_VULKAN(result, "Failed to create a logical device!");
    if (result != VK_SUCCESS || !logical_device) {
        spdlog::critical("Unable to continue without a valid Vulkan logical device.");
        std::abort();
    }

    VULKAN_HPP_DEFAULT_DISPATCHER.init(logical_device);

    //  Queues are created at the same time as the device...
    // So we want handle to queues
    // From given logical device of given queue family, of given queue index (0
    // since only one queue), place reference in given vk::Queue
    graphics_queue = logical_device.getQueue(static_cast<uint32_t>(indices.graphics_family), 0);
    presentation_queue = logical_device.getQueue(static_cast<uint32_t>(indices.presentation_family), 0);
    compute_queue = logical_device.getQueue(static_cast<uint32_t>(indices.compute_family), 0);
}

auto Kataglyphis::VulkanDevice::getQueueFamilies(vk::PhysicalDevice selectedPhysicalDevice)
  -> Kataglyphis::VulkanRendererInternals::QueueFamilyIndices
{
    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices indices{};

    std::vector<vk::QueueFamilyProperties> queue_family_list = selectedPhysicalDevice.getQueueFamilyProperties();

    // Go through each queue family and check if it has at least 1 of required
    // types we need to keep track th eindex by our own
    uint32_t index = 0;
    for (const auto &queue_family : queue_family_list) {
        // first check if queue family has at least 1 queue in that family
        // Queue can be multiple types defined through bitfield. Need to bitwise AND
        // with vk::QueueFlagBits to check if has required  type
        if (queue_family.queueCount > 0 && (queue_family.queueFlags & vk::QueueFlagBits::eGraphics)) {
            indices.graphics_family = static_cast<int>(index);// if queue family valid, than get index
        }

        if (queue_family.queueCount > 0 && (queue_family.queueFlags & vk::QueueFlagBits::eCompute)) {
            indices.compute_family = static_cast<int>(index);
        }

        // check if queue family suppports presentation
        vk::Bool32 presentation_support = selectedPhysicalDevice.getSurfaceSupportKHR(index, *surface).value;
        // check if queue is presentation type (can be both graphics and
        // presentation)
        if (queue_family.queueCount > 0 && presentation_support) {
            indices.presentation_family = static_cast<int>(index);
        }

        // check if queue family indices are in a valid state
        if (indices.is_valid()) { break; }

        index++;
    }

    return indices;
}

auto Kataglyphis::VulkanDevice::getSwapchainDetails(vk::PhysicalDevice device)
  -> Kataglyphis::VulkanRendererInternals::SwapChainDetails
{
    Kataglyphis::VulkanRendererInternals::SwapChainDetails swapchain_details{};
    // get the surface capabilities for the given surface on the given physical
    // device
    swapchain_details.surface_capabilities = device.getSurfaceCapabilitiesKHR(*surface).value;

    // get list of formats
    swapchain_details.formats = device.getSurfaceFormatsKHR(*surface).value;

    // get list of presentation modes
    swapchain_details.presentation_mode = device.getSurfacePresentModesKHR(*surface).value;

    return swapchain_details;
}

auto Kataglyphis::VulkanDevice::check_device_suitable(vk::PhysicalDevice device) -> bool
{
    vk::PhysicalDeviceFeatures device_features = device.getFeatures();

    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices indices = getQueueFamilies(device);

    bool const extensions_supported = check_device_extension_support(device);

    bool swap_chain_valid = false;

    if (extensions_supported) {
        Kataglyphis::VulkanRendererInternals::SwapChainDetails const swap_chain_details = getSwapchainDetails(device);
        swap_chain_valid = !swap_chain_details.presentation_mode.empty() && !swap_chain_details.formats.empty();
    }

    return indices.is_valid() && extensions_supported && swap_chain_valid && device_features.samplerAnisotropy;
}

auto Kataglyphis::VulkanDevice::check_device_extension_support(vk::PhysicalDevice device) -> bool
{
    std::vector<vk::ExtensionProperties> extensions = device.enumerateDeviceExtensionProperties().value;

    if (extensions.empty()) { return false; }

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

auto Kataglyphis::VulkanDevice::getBufferDeviceAddress(const vk::BufferDeviceAddressInfo &info) const
  -> vk::DeviceAddress
{
    return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetBufferDeviceAddress(
      static_cast<VkDevice>(logical_device), reinterpret_cast<const VkBufferDeviceAddressInfo *>(&info));
}
