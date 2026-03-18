module;

#include <vector>
#include <vulkan/vulkan.hpp>
#include "renderer/SwapChainDetails.hpp"

export module kataglyphis.vulkan.device;

import kataglyphis.vulkan.instance;
import kataglyphis.vulkan.queue_family_indices;

export namespace Kataglyphis {
class VulkanDevice
{
  public:
    VulkanDevice(VulkanInstance *instance, vk::SurfaceKHR *surface);

    vk::PhysicalDeviceProperties getPhysicalDeviceProperties() { return device_properties; };
    vk::PhysicalDevice getPhysicalDevice() const { return physical_device; };
    vk::Device getLogicalDevice() const { return logical_device; };
    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices getQueueFamilies();
    vk::Queue getGraphicsQueue() const { return graphics_queue; };
    vk::Queue getComputeQueue() const { return compute_queue; };
    vk::Queue getPresentationQueue() const { return presentation_queue; };
    Kataglyphis::VulkanRendererInternals::SwapChainDetails getSwapchainDetails();
    bool supportsHardwareAcceleratedRRT() { return deviceSupportsHardwareAcceleratedRRT; };
    bool supportsBufferDeviceAddress() const { return deviceSupportsBufferDeviceAddress; };

    void cleanUp();

    ~VulkanDevice();

  private:
    vk::PhysicalDevice physical_device{};
    vk::PhysicalDeviceProperties device_properties{};

    vk::Device logical_device{};

    VulkanInstance *instance;
    vk::SurfaceKHR *surface;

    vk::Queue graphics_queue{};
    vk::Queue presentation_queue{};
    vk::Queue compute_queue{};
    bool deviceSupportsHardwareAcceleratedRRT = true;
    bool deviceSupportsBufferDeviceAddress = false;

    void get_physical_device();
    void create_logical_device();

    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices getQueueFamilies(vk::PhysicalDevice selectedPhysicalDevice);
    Kataglyphis::VulkanRendererInternals::SwapChainDetails getSwapchainDetails(vk::PhysicalDevice device);

    bool check_device_suitable(vk::PhysicalDevice device);
    bool check_device_extension_support(vk::PhysicalDevice device);

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
