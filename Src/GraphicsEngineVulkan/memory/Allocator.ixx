module;

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.allocator;

export namespace Kataglyphis {

class Allocator
{
  public:
    Allocator();
    Allocator(const vk::Device &device,
      const vk::PhysicalDevice &physicalDevice,
      const vk::Instance &instance,
      bool enableBufferDeviceAddress = true);

    VmaAllocator getVmaAllocator() const { return vmaAllocator; }

    void cleanUp();

    ~Allocator();

  private:
    VmaAllocator vmaAllocator{ VK_NULL_HANDLE };
};
}// namespace Kataglyphis
