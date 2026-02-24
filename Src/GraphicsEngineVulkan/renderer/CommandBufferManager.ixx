module;

#include <vulkan/vulkan.h>

export module kataglyphis.vulkan.command_buffer_manager;

export namespace Kataglyphis::VulkanRendererInternals {
class CommandBufferManager
{
  public:
    CommandBufferManager();

    static VkCommandBuffer beginCommandBuffer(VkDevice device, VkCommandPool command_pool);
    static void endAndSubmitCommandBuffer(VkDevice device,
      VkCommandPool command_pool,
      VkQueue queue,
      VkCommandBuffer &command_buffer);

    ~CommandBufferManager() = default;

  private:
};
}// namespace Kataglyphis::VulkanRendererInternals