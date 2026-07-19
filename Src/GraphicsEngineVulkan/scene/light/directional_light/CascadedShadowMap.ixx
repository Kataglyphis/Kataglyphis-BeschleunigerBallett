module;
#include <vector>
#include <memory>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

export module kataglyphis.vulkan.cascaded_shadow_map;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.buffer;

export namespace Kataglyphis {

struct CascadeData {
    float splitDepth;
    glm::mat4 viewProjMatrix;
};

class CascadedShadowMap
{
  public:
    CascadedShadowMap() = default;

    CascadedShadowMap(const CascadedShadowMap &) = delete;
    CascadedShadowMap &operator=(const CascadedShadowMap &) = delete;

    void init(std::shared_ptr<VulkanDevice>device, uint32_t width, uint32_t height, uint32_t num_cascades);

    void createGraphicsPipeline();
    void recordCommands(vk::CommandBuffer &commandBuffer, uint32_t image_index, Scene *scene, const std::vector<vk::DescriptorSet> &descriptorSets);

    Kataglyphis::Texture* getShadowMapArray() { return shadowMapArray.get(); }
    std::vector<vk::Framebuffer>& getFramebuffers() { return framebuffers; }
    vk::RenderPass getRenderPass() const { return renderPass; }

    uint32_t getWidth() const { return shadowWidth; }
    uint32_t getHeight() const { return shadowHeight; }
    uint32_t getNumCascades() const { return numCascades; }

    void updateCascades(const glm::mat4& cameraView, float cameraFov, float aspect, float nearPlane, float farPlane, const glm::vec3& lightDir);
    const std::vector<CascadeData>& getCascadeData() const { return cascadeData; }

    void cleanUp();
    ~CascadedShadowMap() { cleanUp(); }

  private:
    std::shared_ptr<VulkanDevice>device{ nullptr };
    uint32_t shadowWidth{ 0 };
    uint32_t shadowHeight{ 0 };
    uint32_t numCascades{ 0 };

    std::unique_ptr<Kataglyphis::Texture> shadowMapArray;
    vk::RenderPass renderPass;
    std::vector<vk::Framebuffer> framebuffers;

    vk::Pipeline graphicsPipeline{};
    vk::PipelineLayout pipelineLayout{};
    vk::DescriptorSetLayout descriptorSetLayout{};
    vk::DescriptorPool descriptorPool{};
    vk::DescriptorSet descriptorSet{};
    VulkanBuffer lightMatricesBuffer;

    std::vector<CascadeData> cascadeData;

    void createRenderPass();
    void createFramebuffers();
    void createDescriptorSetAndPipeline();
    std::vector<glm::vec4> getFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view);
};
}