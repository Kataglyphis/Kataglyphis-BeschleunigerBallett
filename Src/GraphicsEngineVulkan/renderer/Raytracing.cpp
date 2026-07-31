module;
#include <memory>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "common/MemoryHelper.hpp"
#include "common/Utilities.hpp"
#include <spdlog/spdlog.h>
#include "renderer/pushConstants/PushConstantRayTracing.hpp"

module kataglyphis.vulkan.raytracing;

import kataglyphis.vulkan.shader_helper;
import kataglyphis.vulkan.swapchain;

Kataglyphis::VulkanRendererInternals::Raytracing::Raytracing() = default;

void Kataglyphis::VulkanRendererInternals::Raytracing::init(std::shared_ptr<VulkanDevice>in_device,
  const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts,
  VulkanSwapChain *swapchain)
{
    this->device = in_device;
    this->vulkanSwapChain = swapchain;

    createPCRange();
    createGraphicsPipeline(descriptorSetLayouts);
    createSBT();
}

void Kataglyphis::VulkanRendererInternals::Raytracing::shaderHotReload(
  const std::vector<vk::DescriptorSetLayout> &descriptor_set_layouts)
{
    device->getLogicalDevice().destroyPipeline(graphicsPipeline);
    createGraphicsPipeline(descriptor_set_layouts);
}

void Kataglyphis::VulkanRendererInternals::Raytracing::recordCommands(vk::CommandBuffer &commandBuffer,
  VulkanImage &renderImage,
  [[maybe_unused]] VulkanSwapChain *swapchain,
  const std::vector<vk::DescriptorSet> &descriptorSets)
{
    uint32_t const handle_size = raytracing_properties.shaderGroupHandleSize;
    uint32_t const handle_size_aligned = align_up(handle_size, raytracing_properties.shaderGroupHandleAlignment);

    vk::Device logical_device = device->getLogicalDevice();

    vk::BufferDeviceAddressInfo bufferDeviceAI{};
    bufferDeviceAI.buffer = raygenShaderBindingTableBuffer.getBuffer();

    rgen_region.deviceAddress = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetBufferDeviceAddress(
      static_cast<VkDevice>(logical_device), reinterpret_cast<VkBufferDeviceAddressInfo *>(&bufferDeviceAI));
    rgen_region.stride = handle_size_aligned;
    rgen_region.size = handle_size_aligned;

    bufferDeviceAI.buffer = missShaderBindingTableBuffer.getBuffer();
    miss_region.deviceAddress = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetBufferDeviceAddress(
      static_cast<VkDevice>(logical_device), reinterpret_cast<VkBufferDeviceAddressInfo *>(&bufferDeviceAI));
    miss_region.stride = handle_size_aligned;
    miss_region.size = handle_size_aligned;

    bufferDeviceAI.buffer = hitShaderBindingTableBuffer.getBuffer();
    hit_region.deviceAddress = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetBufferDeviceAddress(
      static_cast<VkDevice>(logical_device), reinterpret_cast<VkBufferDeviceAddressInfo *>(&bufferDeviceAI));
    hit_region.stride = handle_size_aligned;
    hit_region.size = handle_size_aligned;

    pc.clear_color = { 0.0F, 0.0F, 0.0F, 0.0F };
    commandBuffer.pushConstants(pipeline_layout,
      vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eMissKHR,
      0,
      sizeof(PushConstantRaytracing),
      reinterpret_cast<void *>(&pc));

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, graphicsPipeline);

    vk::ImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = 1;

    vk::ImageMemoryBarrier rasterizerToRaytracingImageBarrier{};
    rasterizerToRaytracingImageBarrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    rasterizerToRaytracingImageBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    rasterizerToRaytracingImageBarrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    rasterizerToRaytracingImageBarrier.newLayout = vk::ImageLayout::eGeneral;
    rasterizerToRaytracingImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    rasterizerToRaytracingImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    rasterizerToRaytracingImageBarrier.image = renderImage.getImage();
    rasterizerToRaytracingImageBarrier.subresourceRange = subresourceRange;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
      vk::PipelineStageFlagBits::eRayTracingShaderKHR,
      {},
      {},
      {},
      rasterizerToRaytracingImageBarrier);

    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, pipeline_layout, 0, descriptorSets, {});

    VulkanSwapChain *effectiveSwapChain = swapchain ? swapchain : this->vulkanSwapChain;
    if (!effectiveSwapChain) {
      spdlog::error("Raytracing::recordCommands: VulkanSwapChain is null");
      return;
    }
    const vk::Extent2D &swap_chain_extent = effectiveSwapChain->getSwapChainExtent();
    commandBuffer.traceRaysKHR(
      rgen_region, miss_region, hit_region, call_region, swap_chain_extent.width, swap_chain_extent.height, 1);

    vk::ImageMemoryBarrier raytracingToPostImageBarrier{};
    raytracingToPostImageBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    raytracingToPostImageBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    raytracingToPostImageBarrier.oldLayout = vk::ImageLayout::eGeneral;
    raytracingToPostImageBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    raytracingToPostImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    raytracingToPostImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    raytracingToPostImageBarrier.image = renderImage.getImage();
    raytracingToPostImageBarrier.subresourceRange = subresourceRange;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eRayTracingShaderKHR,
      vk::PipelineStageFlagBits::eFragmentShader,
      {},
      {},
      {},
      raytracingToPostImageBarrier);
}

void Kataglyphis::VulkanRendererInternals::Raytracing::cleanUp()
{
    // Idempotent: safe to call again after an explicit cleanUp (the destructor
    // is only a safety net for the forgotten path). Also covers the case where
    // init() was never called (no hardware raytracing support).
    if (!device) { return; }

    shaderBindingTableBuffer.cleanUp();
    raygenShaderBindingTableBuffer.cleanUp();
    missShaderBindingTableBuffer.cleanUp();
    hitShaderBindingTableBuffer.cleanUp();

    if (graphicsPipeline) {
        device->getLogicalDevice().destroyPipeline(graphicsPipeline);
        graphicsPipeline = nullptr;
    }
    if (pipeline_layout) {
        device->getLogicalDevice().destroyPipelineLayout(pipeline_layout);
        pipeline_layout = nullptr;
    }

    device.reset();
}

Kataglyphis::VulkanRendererInternals::Raytracing::~Raytracing() { cleanUp(); }

void Kataglyphis::VulkanRendererInternals::Raytracing::createPCRange()
{
    pc_ranges.stageFlags =
      vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eMissKHR;
    pc_ranges.offset = 0;
    pc_ranges.size = sizeof(PushConstantRaytracing);
}

void Kataglyphis::VulkanRendererInternals::Raytracing::createGraphicsPipeline(
  const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts)
{
    // Slang-emitted SPIR-V: compiled by compile-slang-shaders.ps1 at build time.
    // Run from the repo root (per AGENTS.md).
    std::string const slang_spv_dir = "Resources/ShadersSlang/build/spirv/raytracing/";

    std::string const raygen_spv = "raytrace.rgen.rgen_main.spv";
    std::string const chit_spv = "raytrace.rchit.rchit_main.spv";
    std::string const miss_spv = "raytrace.rmiss.rmiss_main.spv";
    std::string const shadow_spv = "shadow.rmiss.shadow_rmiss_main.spv";

    vk::ShaderModule raygen_shader_module = loadSpirvShaderModule(device, slang_spv_dir + raygen_spv);
    vk::ShaderModule raychit_shader_module = loadSpirvShaderModule(device, slang_spv_dir + chit_spv);
    vk::ShaderModule raymiss_shader_module = loadSpirvShaderModule(device, slang_spv_dir + miss_spv);
    vk::ShaderModule shadow_shader_module = loadSpirvShaderModule(device, slang_spv_dir + shadow_spv);

    vk::PipelineShaderStageCreateInfo rgen_shader_stage_info{};
    rgen_shader_stage_info.stage = vk::ShaderStageFlagBits::eRaygenKHR;
    rgen_shader_stage_info.module = raygen_shader_module;
    rgen_shader_stage_info.pName = "main";

    vk::PipelineShaderStageCreateInfo rmiss_shader_stage_info{};
    rmiss_shader_stage_info.stage = vk::ShaderStageFlagBits::eMissKHR;
    rmiss_shader_stage_info.module = raymiss_shader_module;
    rmiss_shader_stage_info.pName = "main";

    vk::PipelineShaderStageCreateInfo shadow_shader_stage_info{};
    shadow_shader_stage_info.stage = vk::ShaderStageFlagBits::eMissKHR;
    shadow_shader_stage_info.module = shadow_shader_module;
    shadow_shader_stage_info.pName = "main";

    vk::PipelineShaderStageCreateInfo rchit_shader_stage_info{};
    rchit_shader_stage_info.stage = vk::ShaderStageFlagBits::eClosestHitKHR;
    rchit_shader_stage_info.module = raychit_shader_module;
    rchit_shader_stage_info.pName = "main";

    std::array<vk::PipelineShaderStageCreateInfo, 4> shader_stages = {
        rgen_shader_stage_info, rmiss_shader_stage_info, shadow_shader_stage_info, rchit_shader_stage_info
    };

    enum StageIndices { eRaygen, eMiss, eMiss2, eClosestHit, eShaderGroupCount };

    shader_groups.reserve(4);
    vk::RayTracingShaderGroupCreateInfoKHR shader_group_create_infos[4];

    shader_group_create_infos[0].type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
    shader_group_create_infos[0].generalShader = eRaygen;
    shader_group_create_infos[0].closestHitShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[0].anyHitShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[0].intersectionShader = VK_SHADER_UNUSED_KHR;

    shader_groups.push_back(shader_group_create_infos[0]);

    shader_group_create_infos[1].type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
    shader_group_create_infos[1].generalShader = eMiss;
    shader_group_create_infos[1].closestHitShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[1].anyHitShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[1].intersectionShader = VK_SHADER_UNUSED_KHR;

    shader_groups.push_back(shader_group_create_infos[1]);

    shader_group_create_infos[2].type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
    shader_group_create_infos[2].generalShader = eMiss2;
    shader_group_create_infos[2].closestHitShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[2].anyHitShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[2].intersectionShader = VK_SHADER_UNUSED_KHR;

    shader_groups.push_back(shader_group_create_infos[2]);

    shader_group_create_infos[3].type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
    shader_group_create_infos[3].generalShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[3].closestHitShader = eClosestHit;
    shader_group_create_infos[3].anyHitShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[3].intersectionShader = VK_SHADER_UNUSED_KHR;

    shader_groups.push_back(shader_group_create_infos[3]);

    vk::PipelineLayoutCreateInfo pipeline_layout_create_info{};
    pipeline_layout_create_info.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipeline_layout_create_info.pSetLayouts = descriptorSetLayouts.data();
    pipeline_layout_create_info.pushConstantRangeCount = 1;
    pipeline_layout_create_info.pPushConstantRanges = &pc_ranges;

    vk::Result result =
      device->getLogicalDevice().createPipelineLayout(&pipeline_layout_create_info, nullptr, &pipeline_layout);
    ASSERT_VULKAN(result, "Failed to create raytracing pipeline layout!")

    vk::PipelineLibraryCreateInfoKHR pipeline_library_create_info{};
    pipeline_library_create_info.libraryCount = 0;
    pipeline_library_create_info.pLibraries = nullptr;

    vk::RayTracingPipelineCreateInfoKHR raytracing_pipeline_create_info{};
    raytracing_pipeline_create_info.flags = {};
    raytracing_pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    raytracing_pipeline_create_info.pStages = shader_stages.data();
    raytracing_pipeline_create_info.groupCount = static_cast<uint32_t>(shader_groups.size());
    raytracing_pipeline_create_info.pGroups = shader_groups.data();
    raytracing_pipeline_create_info.maxPipelineRayRecursionDepth = 2;
    raytracing_pipeline_create_info.layout = pipeline_layout;

    vk::Result result2 = device->getLogicalDevice().createRayTracingPipelinesKHR(nullptr,
      device->getPipelineCache(),
      1,
      &raytracing_pipeline_create_info,
      nullptr,
      &graphicsPipeline,
      VULKAN_HPP_DEFAULT_DISPATCHER);
    ASSERT_VULKAN(result2, "Failed to create raytracing pipeline!")

    device->getLogicalDevice().destroyShaderModule(raygen_shader_module);
    device->getLogicalDevice().destroyShaderModule(raymiss_shader_module);
    device->getLogicalDevice().destroyShaderModule(raychit_shader_module);
    device->getLogicalDevice().destroyShaderModule(shadow_shader_module);
}

void Kataglyphis::VulkanRendererInternals::Raytracing::createSBT()
{
    raytracing_properties = vk::PhysicalDeviceRayTracingPipelinePropertiesKHR{};

    vk::PhysicalDeviceProperties2 properties{};
    properties.pNext = &raytracing_properties;

    device->getPhysicalDevice().getProperties2(&properties);

    uint32_t const handle_size = raytracing_properties.shaderGroupHandleSize;
    uint32_t const handle_size_aligned = align_up(handle_size, raytracing_properties.shaderGroupHandleAlignment);

    auto const group_count = static_cast<uint32_t>(shader_groups.size());
    uint32_t const sbt_size = group_count * handle_size_aligned;

    std::vector<uint8_t> handles(sbt_size);

    vk::Result result = device->getLogicalDevice().getRayTracingShaderGroupHandlesKHR(
      graphicsPipeline, 0, group_count, sbt_size, handles.data(), VULKAN_HPP_DEFAULT_DISPATCHER);
    ASSERT_VULKAN(result, "Failed to get ray tracing shader group handles!")

    const vk::BufferUsageFlags bufferUsageFlags =
      vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress;
    const vk::MemoryPropertyFlags memoryPropertyFlags =
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    const vk::MemoryAllocateFlags memoryAllocateFlags = vk::MemoryAllocateFlagBits::eDeviceAddress;

    raygenShaderBindingTableBuffer.create(
      device, handle_size, bufferUsageFlags, memoryPropertyFlags, memoryAllocateFlags);

    missShaderBindingTableBuffer.create(
      device, 2 * handle_size, bufferUsageFlags, memoryPropertyFlags, memoryAllocateFlags);

    hitShaderBindingTableBuffer.create(device, handle_size, bufferUsageFlags, memoryPropertyFlags, memoryAllocateFlags);

    // SBT buffers are host-visible and therefore persistently mapped by VMA.
    void *mapped_raygen = raygenShaderBindingTableBuffer.getMappedData();

    void *mapped_miss = missShaderBindingTableBuffer.getMappedData();

    void *mapped_rchit = hitShaderBindingTableBuffer.getMappedData();

    memcpy(mapped_raygen, handles.data(), handle_size);
    memcpy(mapped_miss, handles.data() + handle_size_aligned, handle_size * 2);
    memcpy(mapped_rchit, handles.data() + (handle_size_aligned * 3), handle_size);
}