module;

#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.command_buffer_manager;

export namespace Kataglyphis::VulkanRendererInternals::CommandBufferManager {
vk::CommandBuffer beginCommandBuffer(vk::Device device, vk::CommandPool command_pool);
[[nodiscard]] auto endAndSubmitCommandBuffer(vk::Device device,
  vk::CommandPool command_pool,
  vk::Queue queue,
  vk::CommandBuffer &command_buffer) -> bool;
}// namespace Kataglyphis::VulkanRendererInternals::CommandBufferManager