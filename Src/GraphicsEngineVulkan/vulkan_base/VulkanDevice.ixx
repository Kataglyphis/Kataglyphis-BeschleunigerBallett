module;

#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>
#include "renderer/SwapChainDetails.hpp"

export module kataglyphis.vulkan.device;

import kataglyphis.vulkan.allocator;
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
    vk::DeviceAddress getBufferDeviceAddress(const vk::BufferDeviceAddressInfo &info) const;
    Allocator &getAllocator() { return allocator; };
    VmaAllocator getVmaAllocator() const { return allocator.getVmaAllocator(); };
    // Minimum alignment for allocations backing buffers whose device address
    // is consumed directly (SBTs, acceleration structure scratch).
    vk::DeviceSize getMinDeviceAddressAlignment() const { return deviceAddressAlignment; };

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
    vk::DeviceSize deviceAddressAlignment{ 1 };

    // VMA allocator owning all buffer/image memory. Created right after the
    // logical device; destroyed in cleanUp() right before the logical device.
    Allocator allocator;

    void get_physical_device();
    void create_logical_device();

    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices getQueueFamilies(vk::PhysicalDevice selectedPhysicalDevice);
    Kataglyphis::VulkanRendererInternals::SwapChainDetails getSwapchainDetails(vk::PhysicalDevice device);

    bool check_device_suitable(vk::PhysicalDevice device);
    bool check_device_extension_support(vk::PhysicalDevice device);
};
}// namespace Kataglyphis
