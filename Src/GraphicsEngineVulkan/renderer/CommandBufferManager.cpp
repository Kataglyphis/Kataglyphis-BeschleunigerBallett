#include "renderer/CommandBufferManager.hpp"

#include "common/Utilities.hpp"

Kataglyphis::VulkanRendererInternals::CommandBufferManager::CommandBufferManager() {}

VkCommandBuffer Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(VkDevice device,
  VkCommandPool command_pool)
{
    // command buffer to hold transfer commands
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;

    // command buffer details
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = command_pool;
    alloc_info.commandBufferCount = 1;

    // allocate command buffer from pool
    VkResult result = vkAllocateCommandBuffers(device, &alloc_info, &command_buffer);
    if (result != VK_SUCCESS || command_buffer == VK_NULL_HANDLE) {
      spdlog::error("Failed to allocate command buffer! (VkResult={})", static_cast<int>(result));
      return VK_NULL_HANDLE;
    }

    // infromation to begin the command buffer record
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    // we are only using the command buffer once, so set up for one time submit
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    // begin recording transfer commands
    result = vkBeginCommandBuffer(command_buffer, &begin_info);
    if (result != VK_SUCCESS) {
      spdlog::error("Failed to begin command buffer! (VkResult={})", static_cast<int>(result));
      vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
      return VK_NULL_HANDLE;
    }

    return command_buffer;
}

void Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(VkDevice device,
  VkCommandPool command_pool,
  VkQueue queue,
  VkCommandBuffer &command_buffer)
{
  if (command_buffer == VK_NULL_HANDLE) {
    spdlog::error("Cannot submit null command buffer.");
    return;
  }

    // end commands
    VkResult result = vkEndCommandBuffer(command_buffer);
  if (result != VK_SUCCESS) {
    spdlog::error("Failed to end command buffer! (VkResult={})", static_cast<int>(result));
    vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
    command_buffer = VK_NULL_HANDLE;
    return;
  }

    // queue submission information
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    // submit transfer command to transfer queue and wait until it finishes
    result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
      spdlog::error("Failed to submit to queue! (VkResult={})", static_cast<int>(result));
      vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
      command_buffer = VK_NULL_HANDLE;
      return;
    }

    result = vkQueueWaitIdle(queue);
    if (result != VK_SUCCESS) {
      spdlog::error("Failed to wait queue idle! (VkResult={})", static_cast<int>(result));
      vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
      command_buffer = VK_NULL_HANDLE;
      return;
    }

    // free temporary command buffer back to pool
    vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
    command_buffer = VK_NULL_HANDLE;
}

Kataglyphis::VulkanRendererInternals::CommandBufferManager::~CommandBufferManager() {}
