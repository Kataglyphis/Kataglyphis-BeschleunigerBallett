module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

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

void Kataglyphis::VulkanRendererInternals::PathTracing::init(VulkanDevice *device,
  const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts)
{
    this->device = device;

    VkPhysicalDeviceProperties const physicalDeviceProps = device->getPhysicalDeviceProperties();
    timeStampPeriod = physicalDeviceProps.limits.timestampPeriod;

    // save the limits for handling all special cases later on
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
  const std::vector<VkDescriptorSetLayout> &descriptor_set_layouts)
{
    vkDestroyPipeline(device->getLogicalDevice(), pipeline, nullptr);
    createPipeline(descriptor_set_layouts);
}

void Kataglyphis::VulkanRendererInternals::PathTracing::recordCommands(VkCommandBuffer &commandBuffer,
  uint32_t /*image_index*/,
  VulkanImage &vulkanImage,
  VulkanSwapChain *vulkanSwapChain,
  const std::vector<VkDescriptorSet> &descriptorSets)
{
    // we have reset the pool; hence start by 0
    uint32_t query = 0;

    vkCmdResetQueryPool(commandBuffer, queryPool, 0, query_count);

    vkCmdWriteTimestamp(
      commandBuffer, VkPipelineStageFlagBits::VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, query++);

    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices const indices = device->getQueueFamilies();

    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.layerCount = 1;

    VkImageMemoryBarrier presentToPathTracingImageBarrier{};
    presentToPathTracingImageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    presentToPathTracingImageBarrier.pNext = nullptr;
    presentToPathTracingImageBarrier.srcQueueFamilyIndex = static_cast<uint32_t>(indices.graphics_family);
    presentToPathTracingImageBarrier.dstQueueFamilyIndex = static_cast<uint32_t>(indices.compute_family);
    presentToPathTracingImageBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    presentToPathTracingImageBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    presentToPathTracingImageBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    presentToPathTracingImageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    presentToPathTracingImageBarrier.subresourceRange = subresourceRange;
    presentToPathTracingImageBarrier.image = vulkanImage.getImage();

    vkCmdPipelineBarrier(commandBuffer,
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,

      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &presentToPathTracingImageBarrier);

    VkExtent2D const imageSize = vulkanSwapChain->getSwapChainExtent();
    push_constant.width = imageSize.width;
    push_constant.height = imageSize.height;
    push_constant.clearColor = { 0.2F, 0.65F, 0.4F, 1.0F };

    vkCmdPushConstants(
      commandBuffer, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantPathTracing), &push_constant);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

    vkCmdBindDescriptorSets(commandBuffer,
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeline_layout,
      0,
      static_cast<uint32_t>(descriptorSets.size()),
      descriptorSets.data(),
      0,
      nullptr);

    uint32_t const workGroupCountX = std::max(
      (imageSize.width + specializationData.specWorkGroupSizeX - 1) / specializationData.specWorkGroupSizeX, 1U);
    uint32_t const workGroupCountY = std::max(
      (imageSize.height + specializationData.specWorkGroupSizeY - 1) / specializationData.specWorkGroupSizeY, 1U);
    uint32_t const workGroupCountZ = 1;

    vkCmdDispatch(commandBuffer, workGroupCountX, workGroupCountY, workGroupCountZ);

    VkImageMemoryBarrier pathTracingToPresentImageBarrier{};
    pathTracingToPresentImageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    pathTracingToPresentImageBarrier.pNext = nullptr;
    pathTracingToPresentImageBarrier.srcQueueFamilyIndex = static_cast<uint32_t>(indices.compute_family);
    pathTracingToPresentImageBarrier.dstQueueFamilyIndex = static_cast<uint32_t>(indices.graphics_family);
    pathTracingToPresentImageBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    pathTracingToPresentImageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    pathTracingToPresentImageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    pathTracingToPresentImageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pathTracingToPresentImageBarrier.image = vulkanImage.getImage();
    pathTracingToPresentImageBarrier.subresourceRange = subresourceRange;

    vkCmdPipelineBarrier(commandBuffer,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &pathTracingToPresentImageBarrier);

    vkCmdWriteTimestamp(
      commandBuffer, VkPipelineStageFlagBits::VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, query++);
}

void Kataglyphis::VulkanRendererInternals::PathTracing::cleanUp()
{
    vkDestroyPipeline(device->getLogicalDevice(), pipeline, nullptr);
    vkDestroyPipelineLayout(device->getLogicalDevice(), pipeline_layout, nullptr);

    vkDestroyQueryPool(device->getLogicalDevice(), queryPool, nullptr);
}

Kataglyphis::VulkanRendererInternals::PathTracing::~PathTracing() = default;

void Kataglyphis::VulkanRendererInternals::PathTracing::createQueryPool()
{
    VkQueryPoolCreateInfo queryPoolInfo = {};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    // This query pool will store pipeline statistics
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.pipelineStatistics = 0;
    queryPoolInfo.queryCount = query_count;
    ASSERT_VULKAN(vkCreateQueryPool(device->getLogicalDevice(), &queryPoolInfo, nullptr, &queryPool),
      "Failed to create query pool!");
}

void Kataglyphis::VulkanRendererInternals::PathTracing::createPipeline(
  const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts)
{
    VkPushConstantRange push_constant_range{};
    push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstantPathTracing);

    VkPipelineLayoutCreateInfo compute_pipeline_layout_create_info{};
    compute_pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    compute_pipeline_layout_create_info.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    compute_pipeline_layout_create_info.pushConstantRangeCount = 1;
    compute_pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;
    compute_pipeline_layout_create_info.pSetLayouts = descriptorSetLayouts.data();

    ASSERT_VULKAN(vkCreatePipelineLayout(
                    device->getLogicalDevice(), &compute_pipeline_layout_create_info, nullptr, &pipeline_layout),
      "Failed to create compute path tracing pipeline layout!");

    // create pipeline
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

    // build shader modules to link to graphics pipeline
    VkShaderModule pathTracingModule = shaderHelper.createShaderModule(device, pathTracingShadercode);

    // Specialization constant for workgroup size
    std::array<VkSpecializationMapEntry, 2> specEntries{};

    specEntries[0].constantID = 0;
    specEntries[0].size = sizeof(specializationData.specWorkGroupSizeX);
    specEntries[0].offset = 0;

    specEntries[1].constantID = 1;
    specEntries[1].size = sizeof(specializationData.specWorkGroupSizeY);
    specEntries[1].offset = offsetof(SpecializationData, specWorkGroupSizeY);

    // specEntries[2].constantID = 2;
    // specEntries[2].size = sizeof(specializationData.specWorkGroupSizeZ);
    // specEntries[2].offset = offsetof(SpecializationData, specWorkGroupSizeZ);

    VkSpecializationInfo specInfo{};
    specInfo.dataSize = sizeof(specializationData);
    specInfo.mapEntryCount = static_cast<uint32_t>(specEntries.size());
    specInfo.pMapEntries = specEntries.data();
    specInfo.pData = &specializationData;

    VkPipelineShaderStageCreateInfo compute_shader_integrate_create_info{};
    compute_shader_integrate_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compute_shader_integrate_create_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compute_shader_integrate_create_info.module = pathTracingModule;
    compute_shader_integrate_create_info.pSpecializationInfo = &specInfo;
    compute_shader_integrate_create_info.pName = "main";

    // -- COMPUTE PIPELINE CREATION --
    VkComputePipelineCreateInfo compute_pipeline_create_info{};
    compute_pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compute_pipeline_create_info.stage = compute_shader_integrate_create_info;
    compute_pipeline_create_info.layout = pipeline_layout;
    compute_pipeline_create_info.flags = 0;
    // create compute pipeline
    ASSERT_VULKAN(vkCreateComputePipelines(
                    device->getLogicalDevice(), VK_NULL_HANDLE, 1, &compute_pipeline_create_info, nullptr, &pipeline),
      "Failed to create a compute pipeline!");

    // Destroy shader modules, no longer needed after pipeline created
    vkDestroyShaderModule(device->getLogicalDevice(), pathTracingModule, nullptr);
}
