module;
#include <vector>
#include <memory>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

export module kataglyphis.vulkan.cascaded_shadow_map;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.frustum;
import kataglyphis.vulkan.buffer;

export namespace Kataglyphis {

struct CascadeData {
    float splitDepth;
    glm::mat4 viewProjMatrix;
};

// Push constants consumed by directional_shadow_map.vert/.geom.
struct ShadowPushConstants
{
    glm::mat4 model;
    uint32_t cascadeIndex;
};

// Free functions so the cascade maths and the caster transform can be tested
// WITHOUT a Vulkan device - CascadedShadowMap itself cannot be constructed
// without one, which is why neither had any coverage while a hard-coded
// identity model matrix silently disabled shadows entirely.

// Splits and light-space matrices for `numCascades` cascades. Pure maths.
//
// shadowDistance clamps how far shadows are fitted, independently of the
// camera far plane - geometry beyond it is simply unshadowed, which is far
// cheaper than spreading the map over space nothing occupies. Pass <= 0 to
// fall back to farPlane (the old behaviour).
//
// splitLambda blends logarithmic (1.0) against uniform (0.0) splits. See the
// measurements in the implementation before raising it: high lambda starves
// subjects that are framed from a distance.
std::vector<CascadeData> computeCascadeData(uint32_t numCascades,
  const glm::mat4 &cameraView,
  float cameraFov,
  float aspect,
  float nearPlane,
  float farPlane,
  const glm::vec3 &lightDir,
  float shadowDistance = 0.0F,
  float splitLambda = 0.5F);

// The caster transform. This exists as a named function purely so a test can
// pin the invariant that was once broken: the shadow pass must transform
// casters by the SAME model matrix as the forward pass, not by identity.
ShadowPushConstants makeShadowPush(const glm::mat4 &modelMatrix, uint32_t cascadeIndex);

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

    void updateCascades(const glm::mat4 &cameraView,
      float cameraFov,
      float aspect,
      float nearPlane,
      float farPlane,
      const glm::vec3 &lightDir,
      float shadowDistance = 0.0F,
      float splitLambda = 0.5F);
    const std::vector<CascadeData>& getCascadeData() const { return cascadeData; }

    /// Caster draws submitted / considered across all cascades in the last
    /// recordCommands call. Summed over cascades, so a 3-cascade scene with
    /// one mesh considers 3.
    unsigned int getCastersDrawn() const { return castersDrawn; }
    unsigned int getCastersConsidered() const { return castersConsidered; }

    void cleanUp();
    ~CascadedShadowMap() { cleanUp(); }

  private:
    unsigned int castersDrawn{ 0 };
    unsigned int castersConsidered{ 0 };
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