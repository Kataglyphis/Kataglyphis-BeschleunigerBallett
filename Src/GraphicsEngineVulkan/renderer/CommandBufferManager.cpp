module;

#include "spdlog/spdlog.h"

#include <string>
#include <vulkan/vulkan_core.h>

module kataglyphis.vulkan.command_buffer_manager;

Kataglyphis::VulkanRendererInternals::CommandBufferManager::CommandBufferManager() = default;

auto Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(VkDevice device,
  VkCommandPool command_pool) -> VkCommandBuffer
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
        spdlog::default_logger_raw()->log(spdlog::level::err,
          std::string("Failed to allocate command buffer! (VkResult=") + std::to_string(static_cast<int>(result))
            + ")");
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
        spdlog::default_logger_raw()->log(spdlog::level::err,
          std::string("Failed to begin command buffer! (VkResult=") + std::to_string(static_cast<int>(result)) + ")");
        command_buffer = VK_NULL_HANDLE;
        return VK_NULL_HANDLE;
    }

    return command_buffer;
}

void Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(VkDevice device,
  VkCommandPool command_pool,
  VkQueue queue,
  VkCommandBuffer &command_buffer)
{
    static_cast<void>(device);
    static_cast<void>(command_pool);

    if (command_buffer == VK_NULL_HANDLE) {
        spdlog::default_logger_raw()->log(spdlog::level::err, "Cannot submit null command buffer.");
        return;
    }

    // end commands
    VkResult result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        spdlog::default_logger_raw()->log(spdlog::level::err,
          std::string("Failed to end command buffer! (VkResult=") + std::to_string(static_cast<int>(result)) + ")");
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
        spdlog::default_logger_raw()->log(spdlog::level::err,
          std::string("Failed to submit to queue! (VkResult=") + std::to_string(static_cast<int>(result)) + ")");
        command_buffer = VK_NULL_HANDLE;
        return;
    }

    result = vkQueueWaitIdle(queue);
    if (result != VK_SUCCESS) {
        spdlog::default_logger_raw()->log(spdlog::level::err,
          std::string("Failed to wait queue idle! (VkResult=") + std::to_string(static_cast<int>(result)) + ")");
        command_buffer = VK_NULL_HANDLE;
        return;
    }

    // Temporary command buffers are released when the command pool is destroyed.
    // Avoid explicit free to prevent freeing potentially pending buffers.
    command_buffer = VK_NULL_HANDLE;
}
