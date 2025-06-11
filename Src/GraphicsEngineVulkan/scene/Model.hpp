#pragma once

#include <memory>
#include <vector>

#include "scene/Mesh.hpp"
#include "scene/Texture.hpp"
namespace Kataglyphis {
class Model
{
  public:
    Model();
    Model(VulkanDevice *device);

    void cleanUp();

    void add_new_mesh(VulkanDevice *device,
      VkQueue transfer_queue,
      VkCommandPool command_pool,
      std::vector<Vertex> &vertices,
      std::vector<unsigned int> &indices,
      std::vector<unsigned int> &materialIndex,
      std::vector<ObjMaterial> &materials);

    uint32_t getTextureCount() { return static_cast<uint32_t>(modelTextures.size()); };
    std::vector<Texture> &getTextures() { return modelTextures; }
    std::vector<VkSampler> &getTextureSamplers() { return modelTextureSamplers; }
    std::vector<std::string> getTextureList() { return texture_list; };
    uint32_t getMeshCount() { return 1; };
    Mesh *getMesh(size_t index) { return &mesh; };
    glm::mat4 getModel() { return model; };
    uint32_t getCustomInstanceIndex() { return mesh_model_index; };
    uint32_t getPrimitiveCount();
    ObjectDescription getObjectDescription() { return mesh.getObjectDescription(); };

    void set_model(glm::mat4 model);
    void addTexture(Texture newTexture);

    ~Model();

  private:
    VulkanDevice *device{ VK_NULL_HANDLE };

    void addSampler(Texture newTexture);

    uint32_t mesh_model_index{ static_cast<uint32_t>(-1) };
    Mesh mesh;
    glm::mat4 model;

    std::vector<std::string> texture_list;
    std::vector<Texture> modelTextures;
    std::vector<VkSampler> modelTextureSamplers;
};
}// namespace Kataglyphis