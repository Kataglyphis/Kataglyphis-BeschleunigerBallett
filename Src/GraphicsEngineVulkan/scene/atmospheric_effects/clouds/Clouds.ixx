module;

#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.clouds;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.texture;

export namespace Kataglyphis {
class Clouds
{
  public:
    Clouds() = default;

    void init(std::shared_ptr<VulkanDevice>device, vk::CommandPool commandPool, vk::DescriptorSetLayout sharedLayout, uint32_t width, uint32_t height);

    void recordComputeCommands(vk::CommandBuffer &commandBuffer, uint32_t image_index, const std::vector<vk::DescriptorSet> &descriptorSets);

    void recreateFrameResources(vk::CommandPool commandPool, uint32_t width, uint32_t height);

    void cleanUp();

    Kataglyphis::Texture* getCloudNoiseTexture() { return cloudNoiseTexture.get(); }
    Kataglyphis::Texture* getCloudOutputTexture() { return cloudOutputTexture.get(); }

    ~Clouds() = default;

  private:
    std::shared_ptr<VulkanDevice>device{ nullptr };

    std::unique_ptr<Kataglyphis::Texture> cloudNoiseTexture;
    std::unique_ptr<Kataglyphis::Texture> cloudOutputTexture;

    vk::DescriptorSetLayout descriptorSetLayout;
    vk::DescriptorPool descriptorPool;
    vk::DescriptorSet descriptorSet;

    vk::DescriptorSetLayout noiseDescriptorSetLayout;
    vk::DescriptorSet noiseDescriptorSet;

    vk::Pipeline noiseComputePipeline;
    vk::PipelineLayout noisePipelineLayout;

    vk::Pipeline cloudComputePipeline;
    vk::PipelineLayout cloudPipelineLayout;

    uint32_t width{ 1920 };
    uint32_t height{ 1080 };

    void createTextures(vk::CommandPool commandPool);
    void createDescriptorSets();
    void createComputePipelines(vk::DescriptorSetLayout sharedLayout);
    void dispatchNoiseGeneration(); // Run once during init
};
}