#include "renderer/CommandBufferManager.hpp"

#include "common/Utilities.hpp"

CommandBufferManager::CommandBufferManager() {}

VkCommandBuffer CommandBufferManager::beginCommandBuffer(VkDevice device, VkCommandPool command_pool)
{
    // command buffer to hold transfer commands
    VkCommandBuffer command_buffer;

    // command buffer details
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = command_pool;
    alloc_info.commandBufferCount = 1;

    // allocate command buffer from pool
    vkAllocateCommandBuffers(device, &alloc_info, &command_buffer);

    // infromation to begin the command buffer record
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    // we are only using the command buffer once, so set up for one time submit
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    // begin recording transfer commands
    vkBeginCommandBuffer(command_buffer, &begin_info);

    return command_buffer;
}

void CommandBufferManager::endAndSubmitCommandBuffer(VkDevice device,
  VkCommandPool command_pool,
  VkQueue queue,
  VkCommandBuffer &command_buffer)
{
    // end commands
    VkResult result = vkEndCommandBuffer(command_buffer);
    ASSERT_VULKAN(result, "Failed to end command buffer!")

    // queue submission information
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    // submit transfer command to transfer queue and wait until it finishes
    result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    ASSERT_VULKAN(result, "Failed to submit to queue!")

    result = vkQueueWaitIdle(queue);
    ASSERT_VULKAN(result, "Failed to wait Idle!")

    // free temporary command buffer back to pool
    vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
}

CommandBufferManager::~CommandBufferManager() {}
