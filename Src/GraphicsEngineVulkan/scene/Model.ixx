module;
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.model;

import kataglyphis.vulkan.obj_material;
import kataglyphis.vulkan.mesh;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.device;
import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.object_description;

export namespace Kataglyphis {
class Model
{
  public:
    Model();
    Model(VulkanDevice *device);

    void cleanUp();

    void add_new_mesh(VulkanDevice *vulkan_device,
      vk::Queue transfer_queue,
      vk::CommandPool command_pool,
      std::vector<Vertex> &vertices,
      std::vector<unsigned int> &indices,
      std::vector<unsigned int> &materialIndex,
      std::vector<ObjMaterial> &materials);

    uint32_t getTextureCount() { return static_cast<uint32_t>(modelTextures.size()); };
    std::vector<Texture> &getTextures() { return modelTextures; }
    std::vector<vk::Sampler> &getTextureSamplers() { return modelTextureSamplers; }
    std::vector<std::string> getTextureList() { return texture_list; };
    uint32_t getMeshCount() { return 1; };
    Mesh *getMesh(size_t /*index*/) { return &mesh; };
    glm::mat4 getModel() { return model; };
    uint32_t getCustomInstanceIndex() { return mesh_model_index; };
    uint32_t getPrimitiveCount();
    ObjectDescription getObjectDescription() { auto __od = mesh.getObjectDescription(); return __od; };

    void set_model(glm::mat4 new_model);
    void addTexture(Texture &&newTexture);

    ~Model();

  private:
    VulkanDevice *device{ nullptr };

    void addSampler(const Texture &newTexture);

    static constexpr uint32_t INVALID_MESH_INDEX = ~uint32_t(0);
    uint32_t mesh_model_index{ INVALID_MESH_INDEX };
    Mesh mesh;
    glm::mat4 model{};

    std::vector<std::string> texture_list;
    std::vector<Texture> modelTextures;
    std::vector<vk::Sampler> modelTextureSamplers;
};
}// namespace Kataglyphis
