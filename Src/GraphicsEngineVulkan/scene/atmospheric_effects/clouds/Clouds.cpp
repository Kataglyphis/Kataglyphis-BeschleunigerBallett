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
    if (!commandBuffer) {
        spdlog::error("Clouds::createStorageTexture: failed to begin command buffer, skipping storage texture creation.");
        return nullptr;
    }
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
    cloudDescriptors.addBinding(0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute)
      .addBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute);
    if (!cloudDescriptors.create(device, 1)) { spdlog::error("Failed to create cloud descriptor resources!"); }

    noiseDescriptors.addBinding(0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute);
    if (!noiseDescriptors.create(device, 1)) { spdlog::error("Failed to create noise descriptor resources!"); }

    cloudDescriptors.writeImage(0, 0, cloudOutputTexture->getImageView(), vk::ImageLayout::eGeneral);
    cloudDescriptors.writeImage(
      0, 1, cloudNoiseTexture->getImageView(), vk::ImageLayout::eGeneral, cloudNoiseTexture->getSampler());
    noiseDescriptors.writeImage(0, 0, cloudNoiseTexture->getImageView(), vk::ImageLayout::eGeneral);
}

void Clouds::createComputePipelines(vk::DescriptorSetLayout sharedLayout)
{
    // Slang-emitted SPIR-V: compiled by compile-slang-shaders.ps1 at build time.
    // Run from the repo root (per AGENTS.md).

    // cloud specific set AND sharedRenderDescriptorSet
    std::array<vk::DescriptorSetLayout, 2> cloudLayouts = { cloudDescriptors.getLayout(), sharedLayout };
    ComputePipelineHandles cloudHandles = createComputePipeline(
      device, "Resources/ShadersSlang/build/spirv/compute/clouds.clouds_main.spv", cloudLayouts);
    cloudPipelineLayout = cloudHandles.layout;
    cloudComputePipeline = cloudHandles.pipeline;

    // Noise pipeline (Slang-emitted SPIR-V)
    std::array<vk::DescriptorSetLayout, 1> noiseLayouts = { noiseDescriptors.getLayout() };
    ComputePipelineHandles noiseHandles = createComputePipeline(
      device, "Resources/ShadersSlang/build/spirv/compute/noise.noise_main.spv", noiseLayouts);
    noisePipelineLayout = noiseHandles.layout;
    noiseComputePipeline = noiseHandles.pipeline;
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
    if (!commandBuffer) {
        spdlog::error("Clouds::dispatchNoiseGeneration: failed to begin command buffer, skipping noise generation.");
        device->getLogicalDevice().destroyCommandPool(commandPool);
        return;
    }

    const vk::DescriptorSet noiseDescriptorSet = noiseDescriptors.sets()[0];
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, noiseComputePipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, noisePipelineLayout, 0, 1, &noiseDescriptorSet, 0, nullptr);

    // noise texture is kNoiseVolumeExtent^3, workgroup size is kNoiseWorkgroupSize^3
    commandBuffer.dispatch(kNoiseVolumeExtent / kNoiseWorkgroupSize,
      kNoiseVolumeExtent / kNoiseWorkgroupSize,
      kNoiseVolumeExtent / kNoiseWorkgroupSize);

    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(device->getLogicalDevice(), commandPool, device->getComputeQueue(), commandBuffer);

    device->getLogicalDevice().destroyCommandPool(commandPool);
}

void Clouds::shaderHotReload(vk::DescriptorSetLayout sharedLayout)
{
    if (cloudComputePipeline) {
        device->getLogicalDevice().destroyPipeline(cloudComputePipeline);
        cloudComputePipeline = nullptr;
    }
    if (cloudPipelineLayout) {
        device->getLogicalDevice().destroyPipelineLayout(cloudPipelineLayout);
        cloudPipelineLayout = nullptr;
    }
    if (noiseComputePipeline) {
        device->getLogicalDevice().destroyPipeline(noiseComputePipeline);
        noiseComputePipeline = nullptr;
    }
    if (noisePipelineLayout) {
        device->getLogicalDevice().destroyPipelineLayout(noisePipelineLayout);
        noisePipelineLayout = nullptr;
    }
    // Descriptor sets and both textures are untouched; the noise volume is
    // content, not a pipeline, so dispatchNoiseGeneration() does not re-run.
    createComputePipelines(sharedLayout);
}

void Clouds::recordComputeCommands(vk::CommandBuffer &commandBuffer, std::span<const vk::DescriptorSet> descriptorSets)
{
    if (descriptorSets.empty()) {
        spdlog::error("Clouds::recordComputeCommands called with an empty shared descriptor set span");
        return;
    }

    // Bind cloud compute pipeline and dispatch thread groups based on screen extent
    const vk::DescriptorSet cloudDescriptorSet = cloudDescriptors.sets()[0];
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, cloudComputePipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, cloudPipelineLayout, 0, 1, &cloudDescriptorSet, 0, nullptr);

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

    cloudDescriptors.writeImage(0, 0, cloudOutputTexture->getImageView(), vk::ImageLayout::eGeneral);
}

void Clouds::cleanUp()
{
    // Idempotent: safe to call again after an explicit cleanUp (the destructor
    // is only a safety net for the forgotten path).
    if (!device) { return; }

    cloudDescriptors.cleanUp();
    noiseDescriptors.cleanUp();
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
