module;

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.allocator;

export namespace Kataglyphis {

class Allocator
{
  public:
    Allocator();
    Allocator(const vk::Device &device, const vk::PhysicalDevice &physicalDevice, const vk::Instance &instance);

    void cleanUp();

    ~Allocator();

  private:
    VmaAllocator vmaAllocator{};
};
}// namespace Kataglyphis
