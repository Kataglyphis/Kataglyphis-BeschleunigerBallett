module;
#include <vector>
#include <memory>
#include <array>
#include <span>
#include <vulkan/vulkan.hpp>
#include "common/FormatHelper.hpp"
#include "common/Utilities.hpp"
#include "scene/atmospheric_effects/clouds/CloudDispatch.hpp"

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

std::unique_ptr<Kataglyphis::Texture> Clouds::createStorageTexture(vk::CommandPool commandPool, uint32_t w, uint32_t h, uint32_t depth, vk::ImageType type, vk::ImageViewType viewType)
{
    auto texture = std::make_unique<Texture>();
    texture->createImage(device, w, h, 1, vk::Format::eR16G16B16A16Sfloat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, 1, vk::ImageCreateFlags{}, type, depth);
    texture->createImageView(device, vk::Format::eR16G16B16A16Sfloat, vk::ImageAspectFlagBits::eColor, 1, viewType, 1);
    texture->createTextureSampler(device);

    vk::CommandBuffer commandBuffer = Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(device->getLogicalDevice(), commandPool);
    texture->getVulkanImage().transitionImageLayout(commandBuffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, 1, vk::ImageAspectFlagBits::eColor);
    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(device->getLogicalDevice(), commandPool, device->getGraphicsQueue(), commandBuffer);

    return texture;
}

void Clouds::createTextures(vk::CommandPool commandPool)
{
    // 3D Texture for Noise
    cloudNoiseTexture = createStorageTexture(
      commandPool, kNoiseVolumeExtent, kNoiseVolumeExtent, kNoiseVolumeExtent, vk::ImageType::e3D, vk::ImageViewType::e3D);

    // 2D Texture for Cloud Output. Assume screen size or half-screen size for performance
    cloudOutputTexture = createStorageTexture(commandPool, width, height, 1, vk::ImageType::e2D, vk::ImageViewType::e2D);
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

void Clouds::createComputePipeline(const char *spirvPath, std::span<const vk::DescriptorSetLayout> setLayouts, vk::PipelineLayout &outLayout, vk::Pipeline &outPipeline)
{
    vk::ShaderModule shaderModule = loadSpirvShaderModule(device, spirvPath);

    vk::PipelineShaderStageCreateInfo stageInfo{};
    stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();

    auto result = device->getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);
    ASSERT_VULKAN(VkResult(result.result), "Failed to create compute pipeline layout!");
    outLayout = result.value;

    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = outLayout;

    auto compResult = device->getLogicalDevice().createComputePipeline(device->getPipelineCache(), pipelineInfo);
    ASSERT_VULKAN(VkResult(compResult.result), "Failed to create compute pipeline!");
    outPipeline = compResult.value;

    device->getLogicalDevice().destroyShaderModule(shaderModule);
}

void Clouds::createComputePipelines(vk::DescriptorSetLayout sharedLayout)
{
    // Slang-emitted SPIR-V: compiled by compile-slang-shaders.ps1 at build time.
    // Run from the repo root (per AGENTS.md).

    // cloud specific set AND sharedRenderDescriptorSet
    std::array<vk::DescriptorSetLayout, 2> cloudLayouts = { descriptorSetLayout, sharedLayout };
    createComputePipeline("Resources/ShadersSlang/build/spirv/compute/clouds.clouds_main.spv", cloudLayouts, cloudPipelineLayout, cloudComputePipeline);

    // Noise pipeline (Slang-emitted SPIR-V)
    std::array<vk::DescriptorSetLayout, 1> noiseLayouts = { noiseDescriptorSetLayout };
    createComputePipeline("Resources/ShadersSlang/build/spirv/compute/noise.noise_main.spv", noiseLayouts, noisePipelineLayout, noiseComputePipeline);
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
    commandPool = poolRes.value;// UNCHECKED_VULKAN_RESULT_OK: noise-dispatch-skip-on-failure

    vk::CommandBuffer commandBuffer = Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(device->getLogicalDevice(), commandPool);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, noiseComputePipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, noisePipelineLayout, 0, 1, &noiseDescriptorSet, 0, nullptr);

    // noise texture is kNoiseVolumeExtent^3, workgroup size is kNoiseWorkgroupSize^3
    commandBuffer.dispatch(kNoiseVolumeExtent / kNoiseWorkgroupSize,
      kNoiseVolumeExtent / kNoiseWorkgroupSize,
      kNoiseVolumeExtent / kNoiseWorkgroupSize);

    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(device->getLogicalDevice(), commandPool, device->getComputeQueue(), commandBuffer);

    device->getLogicalDevice().destroyCommandPool(commandPool);
}

void Clouds::recordComputeCommands(vk::CommandBuffer &commandBuffer, std::span<const vk::DescriptorSet> descriptorSets)
{
    if (descriptorSets.empty()) {
        spdlog::error("Clouds::recordComputeCommands called with an empty shared descriptor set span");
        return;
    }

    // Bind cloud compute pipeline and dispatch thread groups based on screen extent
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, cloudComputePipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, cloudPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    // Also bind the shared rendering descriptor set (which was passed to us as layout 1)
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, cloudPipelineLayout, 1, 1, &descriptorSets[0], 0, nullptr);

    // Image size is dynamically set to width x height
    commandBuffer.dispatch(
      (width + kCloudWorkgroupSize - 1) / kCloudWorkgroupSize, (height + kCloudWorkgroupSize - 1) / kCloudWorkgroupSize, 1);
}

void Clouds::recreateFrameResources(vk::CommandPool commandPool, uint32_t width, uint32_t height)
{
    this->width = width;
    this->height = height;

    if (cloudOutputTexture) {
        cloudOutputTexture->cleanUp();
    }

    cloudOutputTexture = createStorageTexture(commandPool, width, height, 1, vk::ImageType::e2D, vk::ImageViewType::e2D);

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
