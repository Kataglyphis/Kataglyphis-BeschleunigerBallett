module;

#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.command_buffer_manager;

export namespace Kataglyphis::VulkanRendererInternals {
class CommandBufferManager
{
  public:
    CommandBufferManager();

    static vk::CommandBuffer beginCommandBuffer(vk::Device device, vk::CommandPool command_pool);
    static void endAndSubmitCommandBuffer(vk::Device device,
      vk::CommandPool command_pool,
      vk::Queue queue,
      vk::CommandBuffer &command_buffer);

    ~CommandBufferManager() = default;

  private:
};
}// namespace Kataglyphis::VulkanRendererInternals