module;

#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.command_buffer_manager;

export namespace Kataglyphis::VulkanRendererInternals::CommandBufferManager {
vk::CommandBuffer beginCommandBuffer(vk::Device device, vk::CommandPool command_pool);
void endAndSubmitCommandBuffer(vk::Device device,
  vk::CommandPool command_pool,
  vk::Queue queue,
  vk::CommandBuffer &command_buffer);
}// namespace Kataglyphis::VulkanRendererInternals::CommandBufferManager