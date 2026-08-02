module;
#include <memory>
#include <optional>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <sstream>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "common/PipelineLayoutHelper.hpp"
#include "common/Utilities.hpp"
#include "renderer/PathTracingDispatch.hpp"
#include "renderer/pushConstants/PushConstantPathTracing.hpp"

module kataglyphis.vulkan.path_tracing;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.image;
import kataglyphis.vulkan.shader_helper;

// Good source:
// https://github.com/nvpro-samples/vk_mini_path_tracer/blob/main/vk_mini_path_tracer/main.cpp

Kataglyphis::VulkanRendererInternals::PathTracing::PathTracing() = default;

void Kataglyphis::VulkanRendererInternals::PathTracing::init(std::shared_ptr<VulkanDevice>in_device,
  std::span<const vk::DescriptorSetLayout> descriptorSetLayouts)
{
    this->device = in_device;

    createPipeline(descriptorSetLayouts);
}

void Kataglyphis::VulkanRendererInternals::PathTracing::shaderHotReload(
  std::span<const vk::DescriptorSetLayout> descriptor_set_layouts)
{
    device->getLogicalDevice().destroyPipeline(pipeline);
    createPipeline(descriptor_set_layouts);
}

void Kataglyphis::VulkanRendererInternals::PathTracing::recordCommands(vk::CommandBuffer &commandBuffer,
  uint32_t /*image_index*/,
  VulkanImage &vulkanImage,
  VulkanImage &accumulationImage,
  VulkanSwapChain *vulkanSwapChain,
  std::span<const vk::DescriptorSet> descriptorSets,
  uint32_t frame_index,
  uint32_t samples_per_pixel,
  uint32_t max_bounces)
{
    vk::ImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.layerCount = 1;

    vk::ImageMemoryBarrier presentToPathTracingImageBarrier{};
    // This stage is recorded into the frame's graphics command buffer and
    // consumed by a dispatch on that same queue, so there is no queue family
    // ownership transfer to express here: a real transfer needs a paired
    // release on the source queue and acquire on the destination queue,
    // recorded into two separate command buffers submitted to two separate
    // queues, not both halves back-to-back in one command buffer.
    presentToPathTracingImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    presentToPathTracingImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    // eUndefined: the PT compute shader writes every pixel of vulkanImage, so
    // its previous contents (whether the raster pass ran this frame or was
    // skipped because PT owns the frame) are discarded, not read. eUndefined
    // is the only oldLayout valid in both cases.
    presentToPathTracingImageBarrier.srcAccessMask = {};
    presentToPathTracingImageBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    presentToPathTracingImageBarrier.oldLayout = vk::ImageLayout::eUndefined;
    presentToPathTracingImageBarrier.newLayout = vk::ImageLayout::eGeneral;
    presentToPathTracingImageBarrier.subresourceRange = subresourceRange;
    presentToPathTracingImageBarrier.image = vulkanImage.getImage();

    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eVertexShader,
      vk::PipelineStageFlagBits::eComputeShader,
      vk::DependencyFlags{},
      {},
      {},
      { presentToPathTracingImageBarrier });

    // The accumulation history is read-modify-written by every dispatch, and
    // the previous frame's dispatch may still be in flight: make its writes
    // visible before this frame reads them. A pipeline barrier orders against
    // ALL prior commands on the queue, so this also covers the cross-command-
    // buffer frame-to-frame hazard.
    vk::ImageMemoryBarrier accumulationBarrier{};
    accumulationBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    accumulationBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    accumulationBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    accumulationBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
    accumulationBarrier.oldLayout = vk::ImageLayout::eGeneral;
    accumulationBarrier.newLayout = vk::ImageLayout::eGeneral;
    accumulationBarrier.subresourceRange = subresourceRange;
    accumulationBarrier.image = accumulationImage.getImage();

    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
      vk::PipelineStageFlagBits::eComputeShader,
      vk::DependencyFlags{},
      {},
      {},
      { accumulationBarrier });

    vk::Extent2D const imageSize = vulkanSwapChain->getSwapChainExtent();
    push_constant.width = imageSize.width;
    push_constant.height = imageSize.height;
    // Furnace debug mode via environment (KATAGLYPHIS_PT_FURNACE=<radiance>):
    // clearColor carries the uniform environment radiance in rgb and the
    // mode flag in w. The kernel then forces albedo to 1 and replaces the
    // gradient sky with the uniform value - the classic white-furnace test:
    // an unbiased estimator must converge every pixel to EXACTLY the
    // environment radiance, for any geometry. Read per record (NOT a static:
    // several tests share one process, and a frozen first read would pin the
    // mode for all of them); one getenv per frame is noise.
    const char *furnace_value = std::getenv("KATAGLYPHIS_PT_FURNACE");
    if (furnace_value != nullptr && *furnace_value != '\0') {
        const float furnace_radiance = std::strtof(furnace_value, nullptr);
        push_constant.clearColor = { furnace_radiance, furnace_radiance, furnace_radiance, 1.0F };
    } else {
        push_constant.clearColor = { 0.0F, 0.0F, 0.0F, 0.0F };
    }
    push_constant.frame_index = frame_index;
    push_constant.samples_per_pixel = std::max(samples_per_pixel, 1U);
    push_constant.max_bounces = std::max(max_bounces, 1U);

    commandBuffer.pushConstants(
      pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(PushConstantPathTracing), &push_constant);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);

    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline_layout, 0, descriptorSets, nullptr);

    uint32_t const workGroupCountX = std::max(
      (imageSize.width + Kataglyphis::kPathTracingWorkgroupSizeX - 1) / Kataglyphis::kPathTracingWorkgroupSizeX, 1U);
    uint32_t const workGroupCountY = std::max(
      (imageSize.height + Kataglyphis::kPathTracingWorkgroupSizeY - 1) / Kataglyphis::kPathTracingWorkgroupSizeY, 1U);
    uint32_t const workGroupCountZ = 1;

    commandBuffer.dispatch(workGroupCountX, workGroupCountY, workGroupCountZ);

    vk::ImageMemoryBarrier pathTracingToPresentImageBarrier{};
    // Same queue, same command buffer as the barrier above: no ownership
    // transfer to express (see the comment on presentToPathTracingImageBarrier).
    pathTracingToPresentImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pathTracingToPresentImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pathTracingToPresentImageBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    pathTracingToPresentImageBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    pathTracingToPresentImageBarrier.oldLayout = vk::ImageLayout::eGeneral;
    pathTracingToPresentImageBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    pathTracingToPresentImageBarrier.image = vulkanImage.getImage();
    pathTracingToPresentImageBarrier.subresourceRange = subresourceRange;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
      vk::PipelineStageFlagBits::eVertexShader,
      vk::DependencyFlags{},
      {},
      {},
      { pathTracingToPresentImageBarrier });
}

void Kataglyphis::VulkanRendererInternals::PathTracing::cleanUp()
{
    // Idempotent: safe to call again after an explicit cleanUp (the destructor
    // is only a safety net for the forgotten path). Also covers the case where
    // init() was never called (no hardware raytracing support).
    if (!device) { return; }

    if (pipeline) {
        device->getLogicalDevice().destroyPipeline(pipeline);
        pipeline = nullptr;
    }
    if (pipeline_layout) {
        device->getLogicalDevice().destroyPipelineLayout(pipeline_layout);
        pipeline_layout = nullptr;
    }

    device.reset();
}

Kataglyphis::VulkanRendererInternals::PathTracing::~PathTracing() { cleanUp(); }

void Kataglyphis::VulkanRendererInternals::PathTracing::createPipeline(
  std::span<const vk::DescriptorSetLayout> descriptorSetLayouts)
{
    vk::PushConstantRange push_constant_range{};
    push_constant_range.stageFlags = vk::ShaderStageFlagBits::eCompute;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstantPathTracing);

    const std::array<vk::PushConstantRange, 1> push_constant_ranges = { push_constant_range };
    vk::PipelineLayoutCreateInfo compute_pipeline_layout_create_info =
      buildPipelineLayoutCreateInfo(descriptorSetLayouts, push_constant_ranges);

    vk::ResultValue<vk::PipelineLayout> pipeline_layout_result =
      device->getLogicalDevice().createPipelineLayout(compute_pipeline_layout_create_info);
    ASSERT_VULKAN(pipeline_layout_result.result, "Failed to create path tracing pipeline layout!")
    pipeline_layout = pipeline_layout_result.value;

    // Slang-emitted SPIR-V: compiled by compile-slang-shaders.ps1 at build time.
    // Run from the repo root (per AGENTS.md).
    std::string const slang_spv_dir = "Resources/ShadersSlang/build/spirv/path_tracing/";

    std::string const pathTracing_spv = "path_tracing.path_tracing_main.spv";

    vk::ShaderModule pathTracingModule = loadSpirvShaderModule(device, slang_spv_dir + pathTracing_spv);

    vk::PipelineShaderStageCreateInfo compute_shader_integrate_create_info{};
    compute_shader_integrate_create_info.stage = vk::ShaderStageFlagBits::eCompute;
    compute_shader_integrate_create_info.module = pathTracingModule;
    compute_shader_integrate_create_info.pName = "main";

    vk::ComputePipelineCreateInfo compute_pipeline_create_info{};
    compute_pipeline_create_info.stage = compute_shader_integrate_create_info;
    compute_pipeline_create_info.layout = pipeline_layout;
    compute_pipeline_create_info.flags = vk::PipelineCreateFlags{};

    auto result =
      device->getLogicalDevice().createComputePipeline(device->getPipelineCache(), compute_pipeline_create_info);
    if (result.result == vk::Result::eSuccess) {
        pipeline = result.value;
    } else {
        ASSERT_VULKAN(result.result, "Failed to create compute pipeline!")
    }

    device->getLogicalDevice().destroyShaderModule(pathTracingModule);
}