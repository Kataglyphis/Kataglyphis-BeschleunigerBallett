module;
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

export module kataglyphis.vulkan.scene;

import kataglyphis.vulkan.obj_material;
import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.model;
import kataglyphis.vulkan.obj_loader;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.mesh;
import kataglyphis.vulkan.device;
import kataglyphis.vulkan.gui;
import kataglyphis.vulkan.object_description;
import kataglyphis.vulkan.scene_config;
import kataglyphis.vulkan.gui_scene_shared_vars;

export namespace Kataglyphis {
class Scene
{
  public:
    Scene();

    void update_user_input(Kataglyphis::Frontend::GUI *gui);
    void update_model_matrix(glm::mat4 model_matrix, uint32_t model_id);

    const GUISceneSharedVars &getGuiSceneSharedVars() { return guiSceneSharedVars; };

    std::vector<Texture> &getTextures(uint32_t model_index)
    {
        return model_list[static_cast<size_t>(model_index)]->getTextures();
    };
    std::vector<VkSampler> &getTextureSampler(uint32_t model_index)
    {
        return model_list[static_cast<size_t>(model_index)]->getTextureSamplers();
    };
    uint32_t getTextureCount(uint32_t model_index)
    {
        return model_list[static_cast<size_t>(model_index)]->getTextureCount();
    };
    uint32_t getModelCount() { return static_cast<uint32_t>(model_list.size()); };
    glm::mat4 getModelMatrix(uint32_t model_index) { return model_list[static_cast<size_t>(model_index)]->getModel(); };
    uint32_t getMeshCount(uint32_t model_index)
    {
        return static_cast<uint32_t>(model_list[static_cast<size_t>(model_index)]->getMeshCount());
    };
    VkBuffer getVertexBuffer(uint32_t model_index, uint32_t mesh_index)
    {
        return model_list[static_cast<size_t>(model_index)]
          ->getMesh(static_cast<size_t>(mesh_index))
          ->getVertexBuffer();
    };
    VkBuffer getIndexBuffer(uint32_t model_index, uint32_t mesh_index)
    {
        return model_list[static_cast<size_t>(model_index)]->getMesh(static_cast<size_t>(mesh_index))->getIndexBuffer();
    };
    uint32_t getIndexCount(uint32_t model_index, uint32_t mesh_index)
    {
        return model_list[static_cast<size_t>(model_index)]->getMesh(static_cast<size_t>(mesh_index))->getIndexCount();
    };
    uint32_t getNumberObjectDescriptions() { return static_cast<uint32_t>(object_descriptions.size()); };
    uint32_t getNumberMeshes();
    std::vector<ObjectDescription> getObjectDescriptions() { return object_descriptions; };
    std::vector<std::shared_ptr<Model>> const &get_model_list() { return model_list; };

    void loadModel(VulkanDevice *device, VkCommandPool commandPool);

    void add_model(const std::shared_ptr<Model> &model);
    void add_object_description(ObjectDescription object_description);

    void cleanUp();
    ~Scene();

  private:
    std::vector<ObjectDescription> object_descriptions;
    std::vector<std::shared_ptr<Model>> model_list;

    GUISceneSharedVars guiSceneSharedVars;
};
}// namespace Kataglyphis
