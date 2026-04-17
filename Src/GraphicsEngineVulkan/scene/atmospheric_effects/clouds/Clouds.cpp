module;
#include <vector>
#include <memory>
#include <vulkan/vulkan.hpp>
#include "common/FormatHelper.hpp"
#include "common/Utilities.hpp"

module kataglyphis.vulkan.clouds;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.texture;

namespace Kataglyphis {

void Clouds::init(VulkanDevice *device, vk::CommandPool commandPool)
{
    this->device = device;
    createTextures(commandPool);
    createComputePipelines();
    dispatchNoiseGeneration();
}

void Clouds::createTextures(vk::CommandPool commandPool)
{
    // Create 3D Texture for Noise
    cloudNoiseTexture = std::make_unique<Texture>();
    cloudNoiseTexture->createImage(device, 128, 128, 1, vk::Format::eR16G16B16A16Sfloat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, 1, vk::ImageCreateFlags{}, vk::ImageType::e3D, 128);
    cloudNoiseTexture->createImageView(device, vk::Format::eR16G16B16A16Sfloat, vk::ImageAspectFlagBits::eColor, 1, vk::ImageViewType::e3D, 1);
    cloudNoiseTexture->createTextureSampler(device);

    // Create 2D Texture for Cloud Output
    cloudOutputTexture = std::make_unique<Texture>();
    // Assume screen size or half-screen size for performance
    cloudOutputTexture->createImage(device, 1920, 1080, 1, vk::Format::eR16G16B16A16Sfloat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, 1, vk::ImageCreateFlags{}, vk::ImageType::e2D, 1);
    cloudOutputTexture->createImageView(device, vk::Format::eR16G16B16A16Sfloat, vk::ImageAspectFlagBits::eColor, 1, vk::ImageViewType::e2D, 1);
    cloudOutputTexture->createTextureSampler(device);
}

void Clouds::createComputePipelines()
{
    // Placeholder for Compute Pipeline creation for noise and raymarching
    // Will be loaded with ShaderHelper
}

void Clouds::dispatchNoiseGeneration()
{
    // Placeholder to bind noiseComputePipeline and dispatch to fill the 3D texture
}

void Clouds::recordComputeCommands(vk::CommandBuffer &commandBuffer, uint32_t image_index, const std::vector<vk::DescriptorSet> &descriptorSets)
{
    // Bind cloud compute pipeline and dispatch thread groups based on screen extent
}

void Clouds::cleanUp()
{
    if (device) {
        device->getLogicalDevice().destroyPipeline(noiseComputePipeline);
        device->getLogicalDevice().destroyPipelineLayout(noisePipelineLayout);
        device->getLogicalDevice().destroyPipeline(cloudComputePipeline);
        device->getLogicalDevice().destroyPipelineLayout(cloudPipelineLayout);
    }
    if (cloudNoiseTexture) cloudNoiseTexture->cleanUp();
    if (cloudOutputTexture) cloudOutputTexture->cleanUp();
}

}
