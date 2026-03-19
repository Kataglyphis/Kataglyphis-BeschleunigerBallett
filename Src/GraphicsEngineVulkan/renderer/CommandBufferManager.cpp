module;

#include "spdlog/spdlog.h"

#include <string>
#include <vulkan/vulkan.hpp>

module kataglyphis.vulkan.command_buffer_manager;

Kataglyphis::VulkanRendererInternals::CommandBufferManager::CommandBufferManager() = default;

auto Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(vk::Device device,
  vk::CommandPool command_pool) -> vk::CommandBuffer
{
    // command buffer to hold transfer commands
    vk::CommandBuffer command_buffer{};

    // command buffer details
    vk::CommandBufferAllocateInfo alloc_info{};
    alloc_info.level = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandPool = command_pool;
    alloc_info.commandBufferCount = 1;

    // allocate command buffer from pool
    std::vector<vk::CommandBuffer> buffers(1);
    vk::Result const result = device.allocateCommandBuffers(&alloc_info, buffers.data());
    if (result != vk::Result::eSuccess || buffers.empty()) {
        spdlog::default_logger_raw()->log(
          spdlog::level::err, "Failed to allocate command buffer! (vk::Result={})", static_cast<int>(result));
        return vk::CommandBuffer{};
    }
    command_buffer = buffers[0];

    // infromation to begin the command buffer record
    vk::CommandBufferBeginInfo begin_info{};
    // we are only using the command buffer once, so set up for one time submit
    begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    // begin recording transfer commands
    vk::Result const begin_result = command_buffer.begin(&begin_info);
    if (begin_result != vk::Result::eSuccess) {
        spdlog::default_logger_raw()->log(
          spdlog::level::err, "Failed to begin command buffer! (vk::Result={})", static_cast<int>(begin_result));
        return vk::CommandBuffer{};
    }

    return command_buffer;
}

void Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(vk::Device device,
  vk::CommandPool command_pool,
  vk::Queue queue,
  vk::CommandBuffer &command_buffer)
{
    static_cast<void>(device);
    static_cast<void>(command_pool);

    if (!command_buffer) {
        spdlog::default_logger_raw()->log(spdlog::level::err, "Cannot submit null command buffer.");
        return;
    }

    // end commands
    vk::Result const end_result = command_buffer.end();
    if (end_result != vk::Result::eSuccess) {
        spdlog::default_logger_raw()->log(
          spdlog::level::err, "Failed to end command buffer! (vk::Result={})", static_cast<int>(end_result));
        command_buffer = vk::CommandBuffer{};
        return;
    }

    // queue submission information
    vk::SubmitInfo submit_info{};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    // submit transfer command to transfer queue and wait until it finishes
    vk::Result const submit_result = queue.submit(1, &submit_info, vk::Fence{});
    if (submit_result != vk::Result::eSuccess) {
        spdlog::default_logger_raw()->log(
          spdlog::level::err, "Failed to submit to queue! (vk::Result={})", static_cast<int>(submit_result));
        command_buffer = vk::CommandBuffer{};
        return;
    }

    vk::Result const wait_result = queue.waitIdle();
    if (wait_result != vk::Result::eSuccess) {
        spdlog::default_logger_raw()->log(
          spdlog::level::err, "Failed to wait queue idle! (vk::Result={})", static_cast<int>(wait_result));
        command_buffer = vk::CommandBuffer{};
        return;
    }

    // Temporary command buffers are released when the command pool is destroyed.
    // Avoid explicit free to prevent freeing potentially pending buffers.
    command_buffer = vk::CommandBuffer{};
}
