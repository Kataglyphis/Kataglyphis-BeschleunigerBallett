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
    Model(std::shared_ptr<VulkanDevice>device);

    void cleanUp();

    void add_new_mesh(std::shared_ptr<VulkanDevice>vulkan_device,
      vk::Queue transfer_queue,
      vk::CommandPool command_pool,
      std::vector<Vertex> &vertices,
      std::vector<unsigned int> &indices,
      std::vector<unsigned int> &materialIndex,
      std::vector<ObjMaterial> &materials,
      bool double_sided = false);

    uint32_t getTextureCount() { return static_cast<uint32_t>(modelTextures.size()); };
    std::vector<Texture> &getTextures() { return modelTextures; }
    std::vector<vk::Sampler> &getTextureSamplers() { return modelTextureSamplers; }
    std::vector<std::string> getTextureList() { return texture_list; };
    uint32_t getMeshCount() { return static_cast<uint32_t>(meshes.size()); };
    Mesh *getMesh(size_t index) { return &meshes[index]; };
    glm::mat4 getModel() { return model; };
    uint32_t getCustomInstanceIndex() { return mesh_model_index; };
    uint32_t getPrimitiveCount();
    // Kept for the single-mesh callers; the object-description flattening now
    // walks every mesh via getMesh (Scene::add_model). Returns the first mesh's.
    ObjectDescription getObjectDescription()
    {
        return meshes.empty() ? ObjectDescription{} : meshes[0].getObjectDescription();
    };

    void set_model(glm::mat4 new_model);
    void addTexture(Texture &&newTexture);

    ~Model();

  private:
    std::shared_ptr<VulkanDevice>device{ nullptr };

    void addSampler(const Texture &newTexture);

    static constexpr uint32_t INVALID_MESH_INDEX = ~uint32_t(0);
    uint32_t mesh_model_index{ INVALID_MESH_INDEX };
    // A Model holds one or more meshes (the loaders currently produce one, so
    // this is a single-element vector today - the foundation for splitting glTF
    // primitives / OBJ groups into separate meshes). Mesh is move-only (noexcept
    // move), so vector storage is fine.
    std::vector<Mesh> meshes;
    glm::mat4 model{};

    std::vector<std::string> texture_list;
    std::vector<Texture> modelTextures;
    std::vector<vk::Sampler> modelTextureSamplers;
};
}// namespace Kataglyphis
