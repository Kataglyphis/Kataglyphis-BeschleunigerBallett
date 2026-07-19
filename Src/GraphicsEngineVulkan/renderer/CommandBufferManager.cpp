module;

#include "spdlog/spdlog.h"

#include <cstdint>
#include <string>
#include <vulkan/vulkan.hpp>

module kataglyphis.vulkan.command_buffer_manager;

Kataglyphis::VulkanRendererInternals::CommandBufferManager::CommandBufferManager() = default;

auto Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(vk::Device device,
  vk::CommandPool command_pool) -> vk::CommandBuffer
{
    if (!command_pool) {
        spdlog::error("beginCommandBuffer called with VK_NULL_HANDLE commandPool!");
        return vk::CommandBuffer{};
    }

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
    static_cast<void>(command_pool);

    if (!command_buffer) {
        spdlog::default_logger_raw()->log(spdlog::level::err, "Cannot submit null command buffer.");
        return;
    }

    // end commands
    static_cast<void>(command_buffer.end());// MSVC ICE workaround: explicit discard of [[nodiscard]] return

    // queue submission information
    vk::SubmitInfo submit_info{};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    // Submit with a fence and wait only for THIS submission instead of
    // queue.waitIdle(): still fully synchronous for the caller (staging
    // resources may be destroyed right after we return), but other work
    // already queued (e.g. a frame in flight) is no longer serialized
    // behind every upload/layout transition.
    vk::FenceCreateInfo fence_create_info{};
    vk::Fence fence{};
    vk::Result const fence_result = device.createFence(&fence_create_info, nullptr, &fence);
    if (fence_result != vk::Result::eSuccess) {
        spdlog::default_logger_raw()->log(spdlog::level::warn,
          "Failed to create submit fence, falling back to queue.waitIdle()! (vk::Result={})",
          static_cast<int>(fence_result));
        fence = vk::Fence{};
    }

    vk::Result const submit_result = queue.submit(1, &submit_info, fence);
    if (submit_result != vk::Result::eSuccess) {
        spdlog::default_logger_raw()->log(
          spdlog::level::err, "Failed to submit to queue! (vk::Result={})", static_cast<int>(submit_result));
        if (fence) { device.destroyFence(fence); }
        command_buffer = vk::CommandBuffer{};
        return;
    }

    if (fence) {
        vk::Result const wait_result = device.waitForFences(1, &fence, VK_TRUE, UINT64_MAX);
        if (wait_result != vk::Result::eSuccess) {
            spdlog::default_logger_raw()->log(spdlog::level::err,
              "Failed to wait for submit fence, falling back to queue.waitIdle()! (vk::Result={})",
              static_cast<int>(wait_result));
            static_cast<void>(queue.waitIdle());// MSVC ICE workaround: explicit discard of [[nodiscard]] return
        }
        device.destroyFence(fence);
    } else {
        static_cast<void>(queue.waitIdle());// MSVC ICE workaround: explicit discard of [[nodiscard]] return
    }

    // Temporary command buffers are released when the command pool is destroyed.
    // Avoid explicit free to prevent freeing potentially pending buffers.
    command_buffer = vk::CommandBuffer{};
}
