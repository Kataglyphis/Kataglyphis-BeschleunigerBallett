module;
#include <memory>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "common/Utilities.hpp"
#include "renderer/pushConstants/PushConstantPathTracing.hpp"

module kataglyphis.vulkan.path_tracing;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.queue_family_indices;
import kataglyphis.vulkan.file;
import kataglyphis.vulkan.image;
import kataglyphis.vulkan.shader_helper;

// Good source:
// https://github.com/nvpro-samples/vk_mini_path_tracer/blob/main/vk_mini_path_tracer/main.cpp

Kataglyphis::VulkanRendererInternals::PathTracing::PathTracing() = default;

void Kataglyphis::VulkanRendererInternals::PathTracing::init(std::shared_ptr<VulkanDevice>in_device,
  const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts)
{
    this->device = in_device;

    vk::PhysicalDeviceProperties const physicalDeviceProps = device->getPhysicalDeviceProperties();
    timeStampPeriod = physicalDeviceProps.limits.timestampPeriod;

    computeLimits.maxComputeWorkGroupCount[0] = physicalDeviceProps.limits.maxComputeWorkGroupCount[0];
    computeLimits.maxComputeWorkGroupCount[1] = physicalDeviceProps.limits.maxComputeWorkGroupCount[1];
    computeLimits.maxComputeWorkGroupCount[2] = physicalDeviceProps.limits.maxComputeWorkGroupCount[2];

    computeLimits.maxComputeWorkGroupInvocations = physicalDeviceProps.limits.maxComputeWorkGroupInvocations;

    computeLimits.maxComputeWorkGroupSize[0] = physicalDeviceProps.limits.maxComputeWorkGroupSize[0];
    computeLimits.maxComputeWorkGroupSize[1] = physicalDeviceProps.limits.maxComputeWorkGroupSize[1];
    computeLimits.maxComputeWorkGroupSize[2] = physicalDeviceProps.limits.maxComputeWorkGroupSize[2];

    createQueryPool();

    createPipeline(descriptorSetLayouts);
}

void Kataglyphis::VulkanRendererInternals::PathTracing::shaderHotReload(
  const std::vector<vk::DescriptorSetLayout> &descriptor_set_layouts)
{
    device->getLogicalDevice().destroyPipeline(pipeline);
    createPipeline(descriptor_set_layouts);
}

void Kataglyphis::VulkanRendererInternals::PathTracing::recordCommands(vk::CommandBuffer &commandBuffer,
  uint32_t /*image_index*/,
  VulkanImage &vulkanImage,
  VulkanSwapChain *vulkanSwapChain,
  const std::vector<vk::DescriptorSet> &descriptorSets)
{
    uint32_t query = 0;

    commandBuffer.resetQueryPool(queryPool, 0, query_count);

    commandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eComputeShader, queryPool, query++);

    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices const indices = device->getQueueFamilies();

    vk::ImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.layerCount = 1;

    vk::ImageMemoryBarrier presentToPathTracingImageBarrier{};
    presentToPathTracingImageBarrier.srcQueueFamilyIndex = static_cast<uint32_t>(indices.graphics_family);
    presentToPathTracingImageBarrier.dstQueueFamilyIndex = static_cast<uint32_t>(indices.compute_family);
    presentToPathTracingImageBarrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
    presentToPathTracingImageBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    presentToPathTracingImageBarrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    presentToPathTracingImageBarrier.newLayout = vk::ImageLayout::eGeneral;
    presentToPathTracingImageBarrier.subresourceRange = subresourceRange;
    presentToPathTracingImageBarrier.image = vulkanImage.getImage();

    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eVertexShader,
      vk::PipelineStageFlagBits::eComputeShader,
      vk::DependencyFlags{},
      {},
      {},
      { presentToPathTracingImageBarrier });

    vk::Extent2D const imageSize = vulkanSwapChain->getSwapChainExtent();
    push_constant.width = imageSize.width;
    push_constant.height = imageSize.height;
    push_constant.clearColor = { 0.2F, 0.65F, 0.4F, 1.0F };

    commandBuffer.pushConstants(
      pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(PushConstantPathTracing), &push_constant);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);

    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline_layout, 0, descriptorSets, nullptr);

    uint32_t const workGroupCountX = std::max(
      (imageSize.width + specializationData.specWorkGroupSizeX - 1) / specializationData.specWorkGroupSizeX, 1U);
    uint32_t const workGroupCountY = std::max(
      (imageSize.height + specializationData.specWorkGroupSizeY - 1) / specializationData.specWorkGroupSizeY, 1U);
    uint32_t const workGroupCountZ = 1;

    commandBuffer.dispatch(workGroupCountX, workGroupCountY, workGroupCountZ);

    vk::ImageMemoryBarrier pathTracingToPresentImageBarrier{};
    pathTracingToPresentImageBarrier.srcQueueFamilyIndex = static_cast<uint32_t>(indices.compute_family);
    pathTracingToPresentImageBarrier.dstQueueFamilyIndex = static_cast<uint32_t>(indices.graphics_family);
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

    commandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eComputeShader, queryPool, query++);
}

void Kataglyphis::VulkanRendererInternals::PathTracing::cleanUp()
{
    device->getLogicalDevice().destroyPipeline(pipeline);
    device->getLogicalDevice().destroyPipelineLayout(pipeline_layout);

    device->getLogicalDevice().destroyQueryPool(queryPool);
}

Kataglyphis::VulkanRendererInternals::PathTracing::~PathTracing() = default;

void Kataglyphis::VulkanRendererInternals::PathTracing::createQueryPool()
{
    vk::QueryPoolCreateInfo queryPoolInfo{};
    queryPoolInfo.queryType = vk::QueryType::eTimestamp;
    queryPoolInfo.pipelineStatistics = {};
    queryPoolInfo.queryCount = query_count;
    queryPool = device->getLogicalDevice().createQueryPool(queryPoolInfo).value;
}

void Kataglyphis::VulkanRendererInternals::PathTracing::createPipeline(
  const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts)
{
    vk::PushConstantRange push_constant_range{};
    push_constant_range.stageFlags = vk::ShaderStageFlagBits::eCompute;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstantPathTracing);

    vk::PipelineLayoutCreateInfo compute_pipeline_layout_create_info{};
    compute_pipeline_layout_create_info.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    compute_pipeline_layout_create_info.pushConstantRangeCount = 1;
    compute_pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;
    compute_pipeline_layout_create_info.pSetLayouts = descriptorSetLayouts.data();

    pipeline_layout = device->getLogicalDevice().createPipelineLayout(compute_pipeline_layout_create_info).value;

    std::stringstream pathTracing_shader_dir;
    std::filesystem::path const cwd = std::filesystem::current_path();
    pathTracing_shader_dir << cwd.string();
    pathTracing_shader_dir << RELATIVE_RESOURCE_PATH;
    pathTracing_shader_dir << "Shaders/path_tracing/";

    std::string const pathTracing_shader = "path_tracing.comp";

    ShaderHelper shaderHelper;
    File pathTracingShaderFile(shaderHelper.getShaderSpvDir(pathTracing_shader_dir.str(), pathTracing_shader));
    std::vector<char> const pathTracingShadercode = pathTracingShaderFile.readCharSequence();

    shaderHelper.compileShader(pathTracing_shader_dir.str(), pathTracing_shader);

    vk::ShaderModule pathTracingModule = shaderHelper.createShaderModule(device, pathTracingShadercode);

    std::array<vk::SpecializationMapEntry, 2> specEntries{};

    specEntries[0].constantID = 0;
    specEntries[0].size = sizeof(specializationData.specWorkGroupSizeX);
    specEntries[0].offset = 0;

    specEntries[1].constantID = 1;
    specEntries[1].size = sizeof(specializationData.specWorkGroupSizeY);
    specEntries[1].offset = offsetof(SpecializationData, specWorkGroupSizeY);

    vk::SpecializationInfo specInfo{};
    specInfo.dataSize = sizeof(specializationData);
    specInfo.mapEntryCount = static_cast<uint32_t>(specEntries.size());
    specInfo.pMapEntries = specEntries.data();
    specInfo.pData = &specializationData;

    vk::PipelineShaderStageCreateInfo compute_shader_integrate_create_info{};
    compute_shader_integrate_create_info.stage = vk::ShaderStageFlagBits::eCompute;
    compute_shader_integrate_create_info.module = pathTracingModule;
    compute_shader_integrate_create_info.pSpecializationInfo = &specInfo;
    compute_shader_integrate_create_info.pName = "main";

    vk::ComputePipelineCreateInfo compute_pipeline_create_info{};
    compute_pipeline_create_info.stage = compute_shader_integrate_create_info;
    compute_pipeline_create_info.layout = pipeline_layout;
    compute_pipeline_create_info.flags = vk::PipelineCreateFlags{};

    auto result = device->getLogicalDevice().createComputePipeline(nullptr, compute_pipeline_create_info);
    if (result.result == vk::Result::eSuccess) {
        pipeline = result.value;
    } else {
        ASSERT_VULKAN(result.result, "Failed to create compute pipeline!")
    }

    device->getLogicalDevice().destroyShaderModule(pathTracingModule);
}