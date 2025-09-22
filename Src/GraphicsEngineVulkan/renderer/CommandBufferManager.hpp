#pragma once
#include <vulkan/vulkan.h>
namespace Kataglyphis::VulkanRendererInternals {
class CommandBufferManager
{
  public:
    CommandBufferManager();

    VkCommandBuffer beginCommandBuffer(VkDevice device, VkCommandPool command_pool);
    void endAndSubmitCommandBuffer(VkDevice device,
      VkCommandPool command_pool,
      VkQueue queue,
      VkCommandBuffer &command_buffer);

    ~CommandBufferManager();

  private:
};
}// namespace Kataglyphis::VulkanRendererInternals