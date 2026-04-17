module;

#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.sky_box;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.mesh;

export namespace Kataglyphis {
class SkyBox
{
  public:
    SkyBox();

    void init(VulkanDevice *device, vk::CommandPool commandPool);

    void recordCommands(vk::CommandBuffer &commandBuffer, uint32_t image_index, const std::vector<vk::DescriptorSet> &descriptorSets);

    void cleanUp();

    Kataglyphis::Texture* getCubeMapTexture() { return cubeMapTexture.get(); }
    Kataglyphis::Mesh* getMesh() { return skyMesh.get(); }

    ~SkyBox();

  private:
    VulkanDevice *device{ nullptr };

    std::unique_ptr<Kataglyphis::Mesh> skyMesh;
    std::unique_ptr<Kataglyphis::Texture> cubeMapTexture;

    void loadCubeMap(vk::CommandPool commandPool);
    void createMesh(vk::CommandPool commandPool);
};
}