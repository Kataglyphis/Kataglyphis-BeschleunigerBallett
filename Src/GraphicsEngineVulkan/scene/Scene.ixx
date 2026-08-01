module;
#include <optional>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.scene;

import kataglyphis.vulkan.obj_material;
import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.model;
import kataglyphis.vulkan.obj_loader;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.mesh;
import kataglyphis.vulkan.frustum;
import kataglyphis.vulkan.async_model_parse;
import kataglyphis.vulkan.device;
import kataglyphis.vulkan.object_description;
import kataglyphis.vulkan.scene_config;

export namespace Kataglyphis {
class Scene
{
  public:
    Scene();

    void update_model_matrix(glm::mat4 model_matrix, uint32_t model_id);

    std::vector<Texture> &getTextures(uint32_t model_index)
    {
        if (model_index >= model_list.size()) {
            static std::vector<Texture> empty;
            return empty;
        }
        return model_list[static_cast<size_t>(model_index)]->getTextures();
    };
    std::vector<vk::Sampler> &getTextureSampler(uint32_t model_index)
    {
        if (model_index >= model_list.size()) {
            static std::vector<vk::Sampler> empty;
            return empty;
        }
        return model_list[static_cast<size_t>(model_index)]->getTextureSamplers();
    };
    uint32_t getTextureCount(uint32_t model_index)
    {
        if (model_index >= model_list.size()) {
            return 0;
        }
        return model_list[static_cast<size_t>(model_index)]->getTextureCount();
    };
    uint32_t getModelCount() { return static_cast<uint32_t>(model_list.size()); };
    glm::mat4 getModelMatrix(uint32_t model_index) {
        if (model_index >= model_list.size()) {
            return glm::mat4(1.0f);
        }
        return model_list[static_cast<size_t>(model_index)]->getModel();
    };
    uint32_t getMeshCount(uint32_t model_index)
    {
        if (model_index >= model_list.size()) {
            return 0;
        }
        return static_cast<uint32_t>(model_list[static_cast<size_t>(model_index)]->getMeshCount());
    };
    vk::Buffer getVertexBuffer(uint32_t model_index, uint32_t mesh_index)
    {
        if (model_index >= model_list.size()) {
            return vk::Buffer{};
        }
        Mesh *mesh = model_list[static_cast<size_t>(model_index)]->getMesh(static_cast<size_t>(mesh_index));
        return mesh != nullptr ? mesh->getVertexBuffer() : vk::Buffer{};
    };
    vk::Buffer getIndexBuffer(uint32_t model_index, uint32_t mesh_index)
    {
        if (model_index >= model_list.size()) {
            return vk::Buffer{};
        }
        Mesh *mesh = model_list[static_cast<size_t>(model_index)]->getMesh(static_cast<size_t>(mesh_index));
        return mesh != nullptr ? mesh->getIndexBuffer() : vk::Buffer{};
    };
    uint32_t getIndexCount(uint32_t model_index, uint32_t mesh_index)
    {
        if (model_index >= model_list.size()) {
            return 0;
        }
        Mesh *mesh = model_list[static_cast<size_t>(model_index)]->getMesh(static_cast<size_t>(mesh_index));
        return mesh != nullptr ? mesh->getIndexCount() : 0;
    };
    /// glTF material.doubleSided for a mesh, so the raster pass can disable
    /// back-face culling for it. Out-of-range defaults to single-sided.
    bool isMeshDoubleSided(uint32_t model_index, uint32_t mesh_index)
    {
        if (model_index >= model_list.size()) { return false; }
        Mesh *mesh = model_list[static_cast<size_t>(model_index)]->getMesh(static_cast<size_t>(mesh_index));
        return mesh != nullptr && mesh->isDoubleSided();
    };
    /// Object-space bounds of a mesh, for frustum culling. An invalid box is
    /// returned for an out-of-range index, which isVisible() treats as
    /// visible - a missing bound must never be a reason to skip a draw.
    const AABB &getMeshBounds(uint32_t model_index, uint32_t mesh_index)
    {
        static const AABB unknown{ glm::vec3(1.0F), glm::vec3(-1.0F) };
        if (model_index >= model_list.size()) { return unknown; }
        Mesh *mesh = model_list[static_cast<size_t>(model_index)]->getMesh(static_cast<size_t>(mesh_index));
        return mesh != nullptr ? mesh->getBounds() : unknown;
    };
    std::vector<ObjectDescription> getObjectDescriptions() { return object_descriptions; };
    std::vector<std::shared_ptr<Model>> const &get_model_list() { return model_list; };

    void loadModel(std::shared_ptr<VulkanDevice>device, vk::CommandPool commandPool);

    /// Starts parsing the configured model on a worker thread and returns
    /// immediately. The scene has NO model until pollModelLoad() reports one.
    ///
    /// The parse is ~2800 ms of the ~2815 ms load (measured on the bundled
    /// 27 MB model), and the window froze for all of it.
    void beginModelLoadAsync();

    /// True between beginModelLoadAsync() and the model being installed.
    [[nodiscard]] bool isModelLoadPending() const;

    /// Finishes a completed async load: uploads on THIS thread (the one that
    /// owns the device) and adds the model. Returns true on the frame the
    /// model becomes available, so the caller can rebuild whatever depends on
    /// scene contents. Cheap to call every frame.
    bool pollModelLoad(std::shared_ptr<VulkanDevice> device, vk::CommandPool commandPool);

    void reloadModel(std::shared_ptr<VulkanDevice>device, vk::CommandPool commandPool, const std::string &modelPath);

    /// Loads an ADDITIONAL model, leaving existing ones in place, and returns
    /// its model index (the value the raster paths push as objectIndex) or
    /// std::nullopt if loading failed.
    ///
    /// loadModel() deliberately loads exactly one model from SceneConfig and
    /// reloadModel() replaces the scene, so until now nothing could produce a
    /// scene with two models - which meant the per-draw objectIndex could not
    /// be exercised even though the shaders index with it.
    std::optional<uint32_t> loadAdditionalModel(std::shared_ptr<VulkanDevice> device,
      vk::CommandPool commandPool,
      const std::string &modelPath,
      const glm::mat4 &modelMatrix);

    void add_model(const std::shared_ptr<Model> &model);

    void cleanUp();
    ~Scene();

  private:
    AsyncModelParse pendingModelParse;
    bool modelLoadPending{ false };
    std::vector<ObjectDescription> object_descriptions;
    std::vector<std::shared_ptr<Model>> model_list;

};
}// namespace Kataglyphis
