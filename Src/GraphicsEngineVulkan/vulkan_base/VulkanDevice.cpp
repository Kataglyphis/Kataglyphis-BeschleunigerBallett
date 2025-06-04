#include "vulkan_base/VulkanDevice.hpp"

#include <string.h>

#include "common/Utilities.hpp"
#include "spdlog/spdlog.h"
#include <set>
#include <stdexcept>
#include <string>

Kataglyphis::VulkanDevice::VulkanDevice(VulkanInstance *instance, VkSurfaceKHR *surface)
{
    this->instance = instance;
    this->surface = surface;
    get_physical_device();
    create_logical_device();
}

Kataglyphis::VulkanRendererInternals::SwapChainDetails Kataglyphis::VulkanDevice::getSwapchainDetails()
{
    return getSwapchainDetails(physical_device);
}

void Kataglyphis::VulkanDevice::cleanUp() { vkDestroyDevice(logical_device, nullptr); }

Kataglyphis::VulkanDevice::~VulkanDevice() {}

Kataglyphis::VulkanRendererInternals::QueueFamilyIndices Kataglyphis::VulkanDevice::getQueueFamilies()
{
    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices indices{};

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_family_list(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_family_list.data());

    // Go through each queue family and check if it has at least 1 of required
    // types we need to keep track th eindex by our own
    int index = 0;
    for (const auto &queue_family : queue_family_list) {
        // first check if queue family has at least 1 queue in that family
        // Queue can be multiple types defined through bitfield. Need to bitwise AND
        // with VK_QUE_*_BIT to check if has required  type
        if (queue_family.queueCount > 0 && queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics_family = index;// if queue family valid, than get index
        }

        if (queue_family.queueCount > 0 && queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            indices.compute_family = index;
        }

        // check if queue family suppports presentation
        VkBool32 presentation_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, index, *surface, &presentation_support);
        // check if queue is presentation type (can be both graphics and
        // presentation)
        if (queue_family.queueCount > 0 && presentation_support) { indices.presentation_family = index; }

        // check if queue family indices are in a valid state
        if (indices.is_valid()) { break; }

        index++;
    }

    return indices;
}

void Kataglyphis::VulkanDevice::get_physical_device()
{
    // Enumerate physical devices the vkInstance can access
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance->getVulkanInstance(), &device_count, nullptr);

    // if no devices available, then none support of Vulkan
    if (device_count == 0) { spdlog::error("Can not find GPU's that support Vulkan Instance!"); }

    // Get list of physical devices
    std::vector<VkPhysicalDevice> device_list(device_count);
    vkEnumeratePhysicalDevices(instance->getVulkanInstance(), &device_count, device_list.data());

    for (const auto &device : device_list) {
        if (check_device_suitable(device)) {
            physical_device = device;
            break;
        }
    }

    // get properties of our new device
    vkGetPhysicalDeviceProperties(physical_device, &device_properties);
}

void Kataglyphis::VulkanDevice::create_logical_device()
{
    // get the queue family indices for the chosen physical device
    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices indices = getQueueFamilies();

    // vector for queue creation information and set for family indices
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    std::set<int> queue_family_indices = {
        indices.graphics_family, indices.presentation_family, indices.compute_family
    };

    // Queue the logical device needs to create and info to do so (only 1 for now,
    // will add more later!)
    for (int queue_family_index : queue_family_indices) {
        VkDeviceQueueCreateInfo queue_create_info{};
        queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_info.queueFamilyIndex = queue_family_index;// the index of the family to create a queue from
        queue_create_info.queueCount = 1;// number of queues to create
        float priority = 1.0f;
        queue_create_info.pQueuePriorities = &priority;// Vulkan needs to know how to handle multiple queues, so
                                                       // decide priority (1 = highest)

        queue_create_infos.push_back(queue_create_info);
    }

    // -- ALL EXTENSION WE NEED
    VkPhysicalDeviceDescriptorIndexingFeatures indexing_features{};
    indexing_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    indexing_features.runtimeDescriptorArray = VK_TRUE;
    indexing_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    indexing_features.pNext = nullptr;

    // -- NEEDED FOR QUERING THE DEVICE ADDRESS WHEN CREATING ACCELERATION
    // STRUCTURES
    VkPhysicalDeviceBufferDeviceAddressFeaturesEXT buffer_device_address_features{};
    buffer_device_address_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT;
    buffer_device_address_features.pNext = &indexing_features;
    buffer_device_address_features.bufferDeviceAddress = VK_TRUE;
    buffer_device_address_features.bufferDeviceAddressCaptureReplay = VK_TRUE;
    buffer_device_address_features.bufferDeviceAddressMultiDevice = VK_FALSE;

    // --ENABLE RAY TRACING PIPELINE
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline_features{};
    ray_tracing_pipeline_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    ray_tracing_pipeline_features.pNext = &buffer_device_address_features;
    ray_tracing_pipeline_features.rayTracingPipeline = VK_TRUE;

    // -- ENABLE ACCELERATION STRUCTURES
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features{};
    acceleration_structure_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    acceleration_structure_features.pNext = &ray_tracing_pipeline_features;
    acceleration_structure_features.accelerationStructure = VK_TRUE;
    acceleration_structure_features.accelerationStructureCaptureReplay = VK_TRUE;
    acceleration_structure_features.accelerationStructureIndirectBuild = VK_FALSE;
    acceleration_structure_features.accelerationStructureHostCommands = VK_FALSE;
    acceleration_structure_features.descriptorBindingAccelerationStructureUpdateAfterBind = VK_FALSE;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.maintenance4 = VK_TRUE;
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
    rayQueryFeature.rayQuery = VK_TRUE;

    VkPhysicalDeviceFeatures2 features2{};
    features2.pNext = &rayQueryFeature;
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.features.samplerAnisotropy = VK_TRUE;
    features2.features.shaderInt64 = VK_TRUE;
    features2.features.geometryShader = VK_TRUE;
    features2.features.logicOp = VK_TRUE;

    // -- PREPARE FOR HAVING MORE EXTENSION BECAUSE WE NEED RAYTRACING
    // CAPABILITIES
    std::vector<const char *> extensions(device_extensions);

    // Query available extensions for the physical device
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extensionCount, availableExtensions.data());

    // Helper function to check if an extension is supported
    auto isExtensionSupported = [&availableExtensions](const char *extensionName) {
        for (const auto &ext : availableExtensions) {
            if (strcmp(ext.extensionName, extensionName) == 0) { return true; }
        }
        return false;
    };

    for (const char *extensionName : device_extensions_for_raytracing) {
        if (!isExtensionSupported(extensionName)) {
            deviceSupportsHardwareAcceleratedRRT = false;
            spdlog::info("Required extension not supported: {}", extensionName);
        }
    }

    if (deviceSupportsHardwareAcceleratedRRT) {
        // COPY ALL NECESSARY EXTENSIONS FOR RAYTRACING TO THE EXTENSION
        extensions.insert(
          extensions.begin(), device_extensions_for_raytracing.begin(), device_extensions_for_raytracing.end());
    }

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
    device_create_info.pEnabledFeatures = NULL;

    if (deviceSupportsHardwareAcceleratedRRT) { device_create_info.pNext = &features2; }

    // create logical device for the given physical device
    VkResult result = vkCreateDevice(physical_device, &device_create_info, nullptr, &logical_device);
    ASSERT_VULKAN(result, "Failed to create a logical device!");

    //  Queues are created at the same time as the device...
    // So we want handle to queues
    // From given logical device of given queue family, of given queue index (0
    // since only one queue), place reference in given VkQueue
    vkGetDeviceQueue(logical_device, indices.graphics_family, 0, &graphics_queue);
    vkGetDeviceQueue(logical_device, indices.presentation_family, 0, &presentation_queue);
    vkGetDeviceQueue(logical_device, indices.compute_family, 0, &compute_queue);
}

Kataglyphis::VulkanRendererInternals::QueueFamilyIndices Kataglyphis::VulkanDevice::getQueueFamilies(
  VkPhysicalDevice physical_device)
{
    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices indices{};

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_family_list(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_family_list.data());

    // Go through each queue family and check if it has at least 1 of required
    // types we need to keep track th eindex by our own
    int index = 0;
    for (const auto &queue_family : queue_family_list) {
        // first check if queue family has at least 1 queue in that family
        // Queue can be multiple types defined through bitfield. Need to bitwise AND
        // with VK_QUE_*_BIT to check if has required  type
        if (queue_family.queueCount > 0 && queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics_family = index;// if queue family valid, than get index
        }

        if (queue_family.queueCount > 0 && queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            indices.compute_family = index;
        }

        // check if queue family suppports presentation
        VkBool32 presentation_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, index, *surface, &presentation_support);
        // check if queue is presentation type (can be both graphics and
        // presentation)
        if (queue_family.queueCount > 0 && presentation_support) { indices.presentation_family = index; }

        // check if queue family indices are in a valid state
        if (indices.is_valid()) { break; }

        index++;
    }

    return indices;
}

Kataglyphis::VulkanRendererInternals::SwapChainDetails Kataglyphis::VulkanDevice::getSwapchainDetails(
  VkPhysicalDevice device)
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

bool Kataglyphis::VulkanDevice::check_device_suitable(VkPhysicalDevice device)
{
    // Information about device itself (ID, name, type, vendor, etc)
    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(device, &device_properties);

    VkPhysicalDeviceFeatures device_features;
    vkGetPhysicalDeviceFeatures(device, &device_features);

    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices indices = getQueueFamilies(device);

    bool extensions_supported = check_device_extension_support(device);

    bool swap_chain_valid = false;

    if (extensions_supported) {
        Kataglyphis::VulkanRendererInternals::SwapChainDetails swap_chain_details = getSwapchainDetails(device);
        swap_chain_valid = !swap_chain_details.presentation_mode.empty() && !swap_chain_details.formats.empty();
    }

    return indices.is_valid() && extensions_supported && swap_chain_valid && device_features.samplerAnisotropy;
}

bool Kataglyphis::VulkanDevice::check_device_extension_support(VkPhysicalDevice device)
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
