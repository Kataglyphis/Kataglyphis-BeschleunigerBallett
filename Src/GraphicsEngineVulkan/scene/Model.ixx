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
// Re-exported: GltfSamplerDesc appears in addTexture()'s parameter type, so
// consumers that import this module can pass one without a second import.
export import kataglyphis.vulkan.sampler_builder;

export namespace Kataglyphis {
class Model
{
  public:
    Model();
    Model(const std::shared_ptr<VulkanDevice> &device);

    void cleanUp();

    void add_new_mesh(const std::shared_ptr<VulkanDevice> &vulkan_device,
      vk::CommandPool command_pool,
      std::vector<Vertex> &vertices,
      std::vector<unsigned int> &indices,
      std::vector<unsigned int> &materialIndex,
      std::vector<ObjMaterial> &materials,
      bool double_sided = false);

    uint32_t getTextureCount() { return static_cast<uint32_t>(modelTextures.size()); };
    std::vector<Texture> &getTextures() { return modelTextures; }
    std::vector<vk::Sampler> &getTextureSamplers() { return modelTextureSamplers; }
    uint32_t getMeshCount() { return static_cast<uint32_t>(meshes.size()); };
    Mesh *getMesh(size_t index) { return index < meshes.size() ? &meshes[index] : nullptr; };
    glm::mat4 getModel() { return model; };

    void set_model(glm::mat4 new_model);
    void addTexture(Texture &&newTexture, GltfSamplerDesc samplerDesc = {});

    ~Model();

  private:
    std::shared_ptr<VulkanDevice>device{ nullptr };

    void addSampler(const Texture &newTexture, const GltfSamplerDesc &samplerDesc);

    // A Model holds one or more meshes: one per glTF primitive / OBJ group.
    // Mesh is move-only (noexcept move), so vector storage is fine.
    std::vector<Mesh> meshes;
    glm::mat4 model{};

    std::vector<Texture> modelTextures;
    std::vector<vk::Sampler> modelTextureSamplers;

    // Backing storage for the samplers this Model actually created (and
    // therefore owns and must destroy). Distinct from modelTextureSamplers,
    // which stays index-parallel with modelTextures and may repeat a handle
    // when two textures share a mip level AND sampler description.
    std::vector<SamplerKey> ownedSamplerKeys;
    std::vector<vk::Sampler> ownedSamplers;
};
}// namespace Kataglyphis
