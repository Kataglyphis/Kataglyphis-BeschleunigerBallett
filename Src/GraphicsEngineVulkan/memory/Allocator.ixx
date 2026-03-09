module;

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

export module kataglyphis.vulkan.allocator;

export namespace Kataglyphis {

class Allocator
{
  public:
    Allocator();
    Allocator(const VkDevice &device, const VkPhysicalDevice &physicalDevice, const VkInstance &instance);

    void cleanUp();

    ~Allocator();

  private:
    VmaAllocator vmaAllocator{};
};
}// namespace Kataglyphis
