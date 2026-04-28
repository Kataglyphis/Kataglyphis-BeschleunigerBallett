module;

#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.sky_box;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.mesh;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.command_buffer_manager;

export namespace Kataglyphis {
class SkyBox
{
  public:
    SkyBox();

    void init(std::shared_ptr<VulkanDevice>device, vk::CommandPool commandPool);

    void createRenderPass(vk::Format format);
    void createFramebuffers(size_t count, const std::vector<vk::ImageView>& imageViews, uint32_t width, uint32_t height);
    void createGraphicsPipeline(vk::DescriptorSetLayout sharedLayout);
    void recordCommands(vk::CommandBuffer &commandBuffer, uint32_t image_index, const std::vector<vk::DescriptorSet> &descriptorSets);

    void cleanUp();

    Kataglyphis::Texture* getCubeMapTexture() { return cubeMapTexture.get(); }
    Kataglyphis::Mesh* getMesh() { return skyMesh.get(); }
    vk::PipelineLayout getPipelineLayout() { return pipelineLayout; }
    vk::Pipeline getGraphicsPipeline() { return graphicsPipeline; }

    ~SkyBox();

  private:
    std::shared_ptr<VulkanDevice>device{ nullptr };

    std::unique_ptr<Kataglyphis::Mesh> skyMesh;
    std::unique_ptr<Kataglyphis::Texture> cubeMapTexture;

    vk::Pipeline graphicsPipeline{};
    vk::PipelineLayout pipelineLayout{};
    vk::DescriptorSetLayout descriptorSetLayout{};
    vk::DescriptorPool descriptorPool{};
    vk::DescriptorSet descriptorSet{};
    vk::RenderPass renderPass{};
    std::vector<vk::Framebuffer> framebuffers{};
    uint32_t framebufferWidth{0};
    uint32_t framebufferHeight{0};

    void loadCubeMap(vk::CommandPool commandPool);
    void createMesh(vk::CommandPool commandPool);
    void createDescriptorSetForCubeMap();
};
}