#pragma once
#include <vulkan/vulkan.h>

#include <vector>

#include "renderer/QueueFamilyIndices.hpp"
#include "renderer/SwapChainDetails.hpp"
#include "vulkan_base/VulkanInstance.hpp"

/**
 * @brief Handles Vulkan device creation and management.
 *
 * This class is responsible for selecting a physical device, creating the logical device,
 * retrieving queues, and querying swapchain details and hardware capabilities.
 */
class VulkanDevice
{
  public:
    /**
     * @brief Constructs a VulkanDevice object.
     *
     * @param instance Pointer to the VulkanInstance.
     * @param surface Pointer to the Vulkan surface used for presentation.
     */
    VulkanDevice(VulkanInstance *instance, VkSurfaceKHR *surface);

    /**
     * @brief Returns properties of the selected physical device.
     *
     * @return VkPhysicalDeviceProperties containing device details.
     */
    VkPhysicalDeviceProperties getPhysicalDeviceProperties() { return device_properties; };
    VkPhysicalDevice getPhysicalDevice() const { return physical_device; };
    VkDevice getLogicalDevice() const { return logical_device; };
    QueueFamilyIndices getQueueFamilies();
    VkQueue getGraphicsQueue() const { return graphics_queue; };
    VkQueue getComputeQueue() const { return compute_queue; };
    VkQueue getPresentationQueue() const { return presentation_queue; };
    SwapChainDetails getSwapchainDetails();
    bool supportsHardwareAcceleratedRRT() { return deviceSupportsHardwareAcceleratedRRT; };

    void cleanUp();

    ~VulkanDevice();

  private:
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties device_properties;

    VkDevice logical_device;

    VulkanInstance *instance;
    VkSurfaceKHR *surface;

    // available queues
    VkQueue graphics_queue;
    VkQueue presentation_queue;
    VkQueue compute_queue;
    bool deviceSupportsHardwareAcceleratedRRT = true;

    void get_physical_device();
    void create_logical_device();

    QueueFamilyIndices getQueueFamilies(VkPhysicalDevice physical_device);
    SwapChainDetails getSwapchainDetails(VkPhysicalDevice device);

    bool check_device_suitable(VkPhysicalDevice device);
    bool check_device_extension_support(VkPhysicalDevice device);

    const std::vector<const char *> device_extensions = {

        VK_KHR_SWAPCHAIN_EXTENSION_NAME

    };

    // DEVICE EXTENSIONS FOR RAYTRACING
    const std::vector<const char *> device_extensions_for_raytracing = {

        // raytracing related extensions
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        // required from VK_KHR_acceleration_structure
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        // required for pipeline
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        // required by VK_KHR_spirv_1_4
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
        // required for pipeline library
        VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
        // lets start ray queries
        VK_KHR_RAY_QUERY_EXTENSION_NAME

    };
};
