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

    const vk::PhysicalDeviceProperties &getPhysicalDeviceProperties() const { return device_properties; };
    vk::PhysicalDevice getPhysicalDevice() const { return physical_device; };
    vk::Device getLogicalDevice() const { return logical_device; };
    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices getQueueFamilies();
    vk::Queue getGraphicsQueue() const { return graphics_queue; };
    vk::Queue getComputeQueue() const { return compute_queue; };
    vk::Queue getPresentationQueue() const { return presentation_queue; };
    Kataglyphis::VulkanRendererInternals::SwapChainDetails getSwapchainDetails();
    bool supportsHardwareAcceleratedRRT() const { return deviceSupportsHardwareAcceleratedRRT; };
    bool supportsBufferDeviceAddress() const { return deviceSupportsBufferDeviceAddress; };
    bool supportsDepthClamp() const { return deviceSupportsDepthClamp; };
    vk::DeviceAddress getBufferDeviceAddress(const vk::BufferDeviceAddressInfo &info) const;
    Allocator &getAllocator() { return allocator; };
    VmaAllocator getVmaAllocator() const { return allocator.getVmaAllocator(); };
    // Minimum alignment for allocations backing buffers whose device address
    // is consumed directly (SBTs, acceleration structure scratch).
    vk::DeviceSize getMinDeviceAddressAlignment() const { return deviceAddressAlignment; };
    // Device-wide pipeline cache persisted to disk across runs. May be a null
    // handle if cache creation failed; every Vulkan entry point accepts that.
    vk::PipelineCache getPipelineCache() const { return pipeline_cache; };
    // Nanoseconds per timestamp tick (physical-device limit).
    float getTimestampPeriod() const { return device_properties.limits.timestampPeriod; };
    // timestampValidBits of the graphics queue family; 0 means the queue
    // family does not support timestamp queries at all.
    uint32_t getGraphicsQueueTimestampValidBits() const { return graphics_queue_timestamp_valid_bits; };

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
    bool deviceSupportsDepthClamp = false;
    vk::DeviceSize deviceAddressAlignment{ 1 };
    uint32_t graphics_queue_timestamp_valid_bits{ 0 };

    // VMA allocator owning all buffer/image memory. Created right after the
    // logical device; destroyed in cleanUp() right before the logical device.
    Allocator allocator;

    // Pipeline cache seeded from disk on startup and written back in
    // cleanUp(). All cache file I/O failures are non-fatal.
    vk::PipelineCache pipeline_cache{};

    void get_physical_device();
    void create_logical_device();
    void create_pipeline_cache();
    void save_and_destroy_pipeline_cache();

    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices getQueueFamilies(vk::PhysicalDevice selectedPhysicalDevice);
    Kataglyphis::VulkanRendererInternals::SwapChainDetails getSwapchainDetails(vk::PhysicalDevice device);

    bool check_device_suitable(vk::PhysicalDevice device);
    bool check_device_extension_support(vk::PhysicalDevice device);
};
}// namespace Kataglyphis
