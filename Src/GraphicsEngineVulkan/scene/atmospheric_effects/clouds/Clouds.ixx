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

    void init(VulkanDevice *device, vk::CommandPool commandPool);

    void recordComputeCommands(vk::CommandBuffer &commandBuffer, uint32_t image_index, const std::vector<vk::DescriptorSet> &descriptorSets);

    void cleanUp();

    Kataglyphis::Texture* getCloudNoiseTexture() { return cloudNoiseTexture.get(); }
    Kataglyphis::Texture* getCloudOutputTexture() { return cloudOutputTexture.get(); }

    ~Clouds() = default;

  private:
    VulkanDevice *device{ nullptr };

    std::unique_ptr<Kataglyphis::Texture> cloudNoiseTexture;
    std::unique_ptr<Kataglyphis::Texture> cloudOutputTexture;

    vk::Pipeline noiseComputePipeline;
    vk::PipelineLayout noisePipelineLayout;

    vk::Pipeline cloudComputePipeline;
    vk::PipelineLayout cloudPipelineLayout;

    void createTextures(vk::CommandPool commandPool);
    void createComputePipelines();
    void dispatchNoiseGeneration(); // Run once during init
};
}