module;

#include <vector>
#include <vulkan/vulkan.h>
#include "renderer/SwapChainDetails.hpp"

export module kataglyphis.vulkan.device;

import kataglyphis.vulkan.instance;
import kataglyphis.vulkan.queue_family_indices;

export namespace Kataglyphis {
class VulkanDevice
{
  public:
    VulkanDevice(VulkanInstance *instance, VkSurfaceKHR *surface);

    VkPhysicalDeviceProperties getPhysicalDeviceProperties() { return device_properties; };
    VkPhysicalDevice getPhysicalDevice() const { return physical_device; };
    VkDevice getLogicalDevice() const { return logical_device; };
    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices getQueueFamilies();
    VkQueue getGraphicsQueue() const { return graphics_queue; };
    VkQueue getComputeQueue() const { return compute_queue; };
    VkQueue getPresentationQueue() const { return presentation_queue; };
    Kataglyphis::VulkanRendererInternals::SwapChainDetails getSwapchainDetails();
    bool supportsHardwareAcceleratedRRT() { return deviceSupportsHardwareAcceleratedRRT; };
    bool supportsBufferDeviceAddress() const { return deviceSupportsBufferDeviceAddress; };

    void cleanUp();

    ~VulkanDevice();

  private:
    VkPhysicalDevice physical_device{};
    VkPhysicalDeviceProperties device_properties{};

    VkDevice logical_device{};

    VulkanInstance *instance;
    VkSurfaceKHR *surface;

    VkQueue graphics_queue{};
    VkQueue presentation_queue{};
    VkQueue compute_queue{};
    bool deviceSupportsHardwareAcceleratedRRT = true;
    bool deviceSupportsBufferDeviceAddress = false;

    void get_physical_device();
    void create_logical_device();

    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices getQueueFamilies(VkPhysicalDevice selectedPhysicalDevice);
    Kataglyphis::VulkanRendererInternals::SwapChainDetails getSwapchainDetails(VkPhysicalDevice device);

    bool check_device_suitable(VkPhysicalDevice device);
    bool check_device_extension_support(VkPhysicalDevice device);

    const std::vector<const char *> device_extensions = {

        VK_KHR_SWAPCHAIN_EXTENSION_NAME

    };

    const std::vector<const char *> device_extensions_for_raytracing = {

        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
        VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME

    };
};
}// namespace Kataglyphis
