module;
#include <vector>
#include <memory>
#include <array>
#include <vulkan/vulkan.hpp>
#include "common/FormatHelper.hpp"
#include "common/Utilities.hpp"

module kataglyphis.vulkan.clouds;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.shader_helper;
import kataglyphis.vulkan.command_buffer_manager;

namespace Kataglyphis {

void Clouds::init(std::shared_ptr<VulkanDevice>device, vk::CommandPool commandPool, vk::DescriptorSetLayout sharedLayout, uint32_t width, uint32_t height)
{
    this->device = device;
    this->width = width;
    this->height = height;
    createTextures(commandPool);
    createDescriptorSets();
    createComputePipelines(sharedLayout);
    dispatchNoiseGeneration();
}

void Clouds::createTextures(vk::CommandPool commandPool)
{
    // Create 3D Texture for Noise
    cloudNoiseTexture = std::make_unique<Texture>();
    cloudNoiseTexture->createImage(device, 128, 128, 1, vk::Format::eR16G16B16A16Sfloat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, 1, vk::ImageCreateFlags{}, vk::ImageType::e3D, 128);
    cloudNoiseTexture->createImageView(device, vk::Format::eR16G16B16A16Sfloat, vk::ImageAspectFlagBits::eColor, 1, vk::ImageViewType::e3D, 1);
    cloudNoiseTexture->createTextureSampler(device);

    // Transition noise texture
    vk::CommandBuffer commandBuffer = Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(device->getLogicalDevice(), commandPool);
    cloudNoiseTexture->getVulkanImage().transitionImageLayout(commandBuffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, 1, vk::ImageAspectFlagBits::eColor);
    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(device->getLogicalDevice(), commandPool, device->getGraphicsQueue(), commandBuffer);

    // Create 2D Texture for Cloud Output
    cloudOutputTexture = std::make_unique<Texture>();
    // Assume screen size or half-screen size for performance
    cloudOutputTexture->createImage(device, width, height, 1, vk::Format::eR16G16B16A16Sfloat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, 1, vk::ImageCreateFlags{}, vk::ImageType::e2D, 1);
    cloudOutputTexture->createImageView(device, vk::Format::eR16G16B16A16Sfloat, vk::ImageAspectFlagBits::eColor, 1, vk::ImageViewType::e2D, 1);
    cloudOutputTexture->createTextureSampler(device);

    // Transition output texture
    commandBuffer = Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(device->getLogicalDevice(), commandPool);
    cloudOutputTexture->getVulkanImage().transitionImageLayout(commandBuffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, 1, vk::ImageAspectFlagBits::eColor);
    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(device->getLogicalDevice(), commandPool, device->getGraphicsQueue(), commandBuffer);
}

void Clouds::createDescriptorSets()
{
    // Layout
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorCount = 1;
    bindings[0].descriptorType = vk::DescriptorType::eStorageImage;
    bindings[0].pImmutableSamplers = nullptr;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

    bindings[1].binding = 1;
    bindings[1].descriptorCount = 1;
    bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    bindings[1].pImmutableSamplers = nullptr;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    auto result = device->getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    ASSERT_VULKAN(VkResult(result.result), "Failed to create cloud descriptor set layout!");
    descriptorSetLayout = result.value;

    // Pool
    std::array<vk::DescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = vk::DescriptorType::eStorageImage;
    poolSizes[0].descriptorCount = 2; // +1 for noise
    poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[1].descriptorCount = 1;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 2; // 2 sets total

    auto poolResult = device->getLogicalDevice().createDescriptorPool(poolInfo);
    ASSERT_VULKAN(VkResult(poolResult.result), "Failed to create cloud descriptor pool!");
    descriptorPool = poolResult.value;

    // Allocate Set
    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    auto allocResult = device->getLogicalDevice().allocateDescriptorSets(allocInfo);
    ASSERT_VULKAN(VkResult(allocResult.result), "Failed to allocate cloud descriptor sets!");
    descriptorSet = allocResult.value[0];

    // Noise Layout
    vk::DescriptorSetLayoutBinding noiseBinding{};
    noiseBinding.binding = 0;
    noiseBinding.descriptorCount = 1;
    noiseBinding.descriptorType = vk::DescriptorType::eStorageImage;
    noiseBinding.pImmutableSamplers = nullptr;
    noiseBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo noiseLayoutInfo{};
    noiseLayoutInfo.bindingCount = 1;
    noiseLayoutInfo.pBindings = &noiseBinding;

    auto noiseLayoutResult = device->getLogicalDevice().createDescriptorSetLayout(noiseLayoutInfo);
    ASSERT_VULKAN(VkResult(noiseLayoutResult.result), "Failed to create noise descriptor set layout!");
    noiseDescriptorSetLayout = noiseLayoutResult.value;

    // Allocate Noise Set
    vk::DescriptorSetAllocateInfo noiseAllocInfo{};
    noiseAllocInfo.descriptorPool = descriptorPool;
    noiseAllocInfo.descriptorSetCount = 1;
    noiseAllocInfo.pSetLayouts = &noiseDescriptorSetLayout;

    auto noiseAllocResult = device->getLogicalDevice().allocateDescriptorSets(noiseAllocInfo);
    ASSERT_VULKAN(VkResult(noiseAllocResult.result), "Failed to allocate noise descriptor sets!");
    noiseDescriptorSet = noiseAllocResult.value[0];

    // Write Descriptor Set
    vk::DescriptorImageInfo outputImageInfo{};
    outputImageInfo.imageLayout = vk::ImageLayout::eGeneral;
    outputImageInfo.imageView = cloudOutputTexture->getImageView();

    vk::DescriptorImageInfo noiseImageInfo{};
    noiseImageInfo.imageLayout = vk::ImageLayout::eGeneral;
    noiseImageInfo.imageView = cloudNoiseTexture->getImageView();
    noiseImageInfo.sampler = cloudNoiseTexture->getSampler();

    std::array<vk::WriteDescriptorSet, 2> descriptorWrites{};
    descriptorWrites[0].dstSet = descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = vk::DescriptorType::eStorageImage;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pImageInfo = &outputImageInfo;

    descriptorWrites[1].dstSet = descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &noiseImageInfo;

    vk::WriteDescriptorSet noiseWrite{};
    noiseWrite.dstSet = noiseDescriptorSet;
    noiseWrite.dstBinding = 0;
    noiseWrite.dstArrayElement = 0;
    noiseWrite.descriptorType = vk::DescriptorType::eStorageImage;
    noiseWrite.descriptorCount = 1;
    noiseWrite.pImageInfo = &noiseImageInfo;

    std::array<vk::WriteDescriptorSet, 3> allWrites = { descriptorWrites[0], descriptorWrites[1], noiseWrite };

    device->getLogicalDevice().updateDescriptorSets(static_cast<uint32_t>(allWrites.size()), allWrites.data(), 0, nullptr);
}

void Clouds::createComputePipelines(vk::DescriptorSetLayout sharedLayout)
{
    // Slang-emitted SPIR-V: compiled by compile-slang-shaders.ps1 at build time.
    // Run from the repo root (per AGENTS.md).
    vk::ShaderModule cloudShaderModule =
      loadSpirvShaderModule(device, "Resources/ShadersSlang/build/spirv/compute/clouds.clouds_main.spv");

    vk::PipelineShaderStageCreateInfo computeStageInfo{};
    computeStageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    computeStageInfo.module = cloudShaderModule;
    computeStageInfo.pName = "main";

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = 2; // cloud specific set AND sharedRenderDescriptorSet
    
    std::array<vk::DescriptorSetLayout, 2> layouts = { descriptorSetLayout, sharedLayout };
    pipelineLayoutInfo.pSetLayouts = layouts.data();

    auto result = device->getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);
    ASSERT_VULKAN(VkResult(result.result), "Failed to create cloud compute pipeline layout!");
    cloudPipelineLayout = result.value;

    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage = computeStageInfo;
    pipelineInfo.layout = cloudPipelineLayout;

    auto compResult = device->getLogicalDevice().createComputePipeline(device->getPipelineCache(), pipelineInfo);
    ASSERT_VULKAN(VkResult(compResult.result), "Failed to create cloud compute pipeline!");
    cloudComputePipeline = compResult.value;

    device->getLogicalDevice().destroyShaderModule(cloudShaderModule);

    // Noise pipeline (Slang-emitted SPIR-V)
    vk::ShaderModule noiseShaderModule =
      loadSpirvShaderModule(device, "Resources/ShadersSlang/build/spirv/compute/noise.noise_main.spv");

    vk::PipelineShaderStageCreateInfo noiseStageInfo{};
    noiseStageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    noiseStageInfo.module = noiseShaderModule;
    noiseStageInfo.pName = "main";

    vk::PipelineLayoutCreateInfo noiseLayoutInfoCreate{};
    noiseLayoutInfoCreate.setLayoutCount = 1;
    noiseLayoutInfoCreate.pSetLayouts = &noiseDescriptorSetLayout;

    auto noiseLayoutResult = device->getLogicalDevice().createPipelineLayout(noiseLayoutInfoCreate);
    ASSERT_VULKAN(VkResult(noiseLayoutResult.result), "Failed to create noise pipeline layout!");
    noisePipelineLayout = noiseLayoutResult.value;

    vk::ComputePipelineCreateInfo noisePipelineInfo{};
    noisePipelineInfo.stage = noiseStageInfo;
    noisePipelineInfo.layout = noisePipelineLayout;

    auto noiseCompResult =
      device->getLogicalDevice().createComputePipeline(device->getPipelineCache(), noisePipelineInfo);
    ASSERT_VULKAN(VkResult(noiseCompResult.result), "Failed to create noise pipeline!");
    noiseComputePipeline = noiseCompResult.value;

    device->getLogicalDevice().destroyShaderModule(noiseShaderModule);
}

void Clouds::dispatchNoiseGeneration()
{
    // Dispatch to fill the 3D texture
    auto queueFamilies = device->getQueueFamilies();
    if (queueFamilies.compute_family < 0) {
        spdlog::warn("No compute queue family available, skipping noise generation dispatch");
        return;
    }

    vk::CommandPool commandPool;
    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.queueFamilyIndex = static_cast<uint32_t>(queueFamilies.compute_family);
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
    auto poolRes = device->getLogicalDevice().createCommandPool(poolInfo);
    if (poolRes.result != vk::Result::eSuccess) {
        spdlog::error("Failed to create command pool for noise generation");
        return;
    }
    commandPool = poolRes.value;

    vk::CommandBuffer commandBuffer = Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(device->getLogicalDevice(), commandPool);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, noiseComputePipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, noisePipelineLayout, 0, 1, &noiseDescriptorSet, 0, nullptr);

    // noise texture is 128x128x128, local_size is 8x8x8
    commandBuffer.dispatch(128 / 8, 128 / 8, 128 / 8);

    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(device->getLogicalDevice(), commandPool, device->getComputeQueue(), commandBuffer);

    device->getLogicalDevice().destroyCommandPool(commandPool);
}

void Clouds::recordComputeCommands(vk::CommandBuffer &commandBuffer, const std::vector<vk::DescriptorSet> &descriptorSets)
{
    // Bind cloud compute pipeline and dispatch thread groups based on screen extent
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, cloudComputePipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, cloudPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    
    // Also bind the shared rendering descriptor set (which was passed to us as layout 1)
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, cloudPipelineLayout, 1, 1, &descriptorSets[0], 0, nullptr);

    // Image size is dynamically set to width x height
    commandBuffer.dispatch((width + 15) / 16, (height + 15) / 16, 1);
}

void Clouds::recreateFrameResources(vk::CommandPool commandPool, uint32_t width, uint32_t height)
{
    this->width = width;
    this->height = height;

    if (cloudOutputTexture) {
        cloudOutputTexture->cleanUp();
    }

    cloudOutputTexture = std::make_unique<Texture>();
    cloudOutputTexture->createImage(device, width, height, 1, vk::Format::eR16G16B16A16Sfloat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, 1, vk::ImageCreateFlags{}, vk::ImageType::e2D, 1);
    cloudOutputTexture->createImageView(device, vk::Format::eR16G16B16A16Sfloat, vk::ImageAspectFlagBits::eColor, 1, vk::ImageViewType::e2D, 1);
    cloudOutputTexture->createTextureSampler(device);

    vk::CommandBuffer commandBuffer = Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(device->getLogicalDevice(), commandPool);
    cloudOutputTexture->getVulkanImage().transitionImageLayout(commandBuffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, 1, vk::ImageAspectFlagBits::eColor);
    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(device->getLogicalDevice(), commandPool, device->getGraphicsQueue(), commandBuffer);

    // Update descriptor set
    vk::DescriptorImageInfo outputImageInfo{};
    outputImageInfo.imageLayout = vk::ImageLayout::eGeneral;
    outputImageInfo.imageView = cloudOutputTexture->getImageView();

    vk::WriteDescriptorSet descriptorWrite{};
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = vk::DescriptorType::eStorageImage;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &outputImageInfo;

    device->getLogicalDevice().updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
}

void Clouds::cleanUp()
{
    // Idempotent: safe to call again after an explicit cleanUp (the destructor
    // is only a safety net for the forgotten path).
    if (!device) { return; }

    if (descriptorPool) {
        device->getLogicalDevice().destroyDescriptorPool(descriptorPool);
        descriptorPool = nullptr;
    }
    descriptorSet = nullptr;// freed with the pool
    noiseDescriptorSet = nullptr;// freed with the pool
    if (descriptorSetLayout) {
        device->getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout);
        descriptorSetLayout = nullptr;
    }
    if (noiseDescriptorSetLayout) {
        device->getLogicalDevice().destroyDescriptorSetLayout(noiseDescriptorSetLayout);
        noiseDescriptorSetLayout = nullptr;
    }
    if (noiseComputePipeline) {
        device->getLogicalDevice().destroyPipeline(noiseComputePipeline);
        noiseComputePipeline = nullptr;
    }
    if (noisePipelineLayout) {
        device->getLogicalDevice().destroyPipelineLayout(noisePipelineLayout);
        noisePipelineLayout = nullptr;
    }
    if (cloudComputePipeline) {
        device->getLogicalDevice().destroyPipeline(cloudComputePipeline);
        cloudComputePipeline = nullptr;
    }
    if (cloudPipelineLayout) {
        device->getLogicalDevice().destroyPipelineLayout(cloudPipelineLayout);
        cloudPipelineLayout = nullptr;
    }
    if (cloudNoiseTexture) { cloudNoiseTexture->cleanUp(); }
    cloudNoiseTexture.reset();
    if (cloudOutputTexture) { cloudOutputTexture->cleanUp(); }
    cloudOutputTexture.reset();

    device.reset();
}

}
