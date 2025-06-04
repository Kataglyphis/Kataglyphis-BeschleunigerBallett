#pragma once

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#include "Model.hpp"
#include "gui/GUI.hpp"
#include "scene/GUISceneSharedVars.hpp"
#include "scene/Mesh.hpp"

#include "SceneConfig.hpp"

namespace Kataglyphis {
class Scene
{
  public:
    Scene();

    void update_user_input(Kataglyphis::Frontend::GUI *gui);
    void update_model_matrix(glm::mat4 model_matrix, int model_id);

    const GUISceneSharedVars &getGuiSceneSharedVars() { return guiSceneSharedVars; };

    std::vector<Texture> &getTextures(int model_index) { return model_list[model_index]->getTextures(); };
    std::vector<VkSampler> &getTextureSampler(int model_index)
    {
        return model_list[model_index]->getTextureSamplers();
    };
    uint32_t getTextureCount(int model_index) { return model_list[model_index]->getTextureCount(); };
    uint32_t getModelCount() { return static_cast<uint32_t>(model_list.size()); };
    glm::mat4 getModelMatrix(int model_index) { return model_list[model_index]->getModel(); };
    uint32_t getMeshCount(int model_index) { return static_cast<uint32_t>(model_list[model_index]->getMeshCount()); };
    VkBuffer getVertexBuffer(int model_index, int mesh_index)
    {
        return model_list[model_index]->getMesh(mesh_index)->getVertexBuffer();
    };
    VkBuffer getIndexBuffer(int model_index, int mesh_index)
    {
        return model_list[model_index]->getMesh(mesh_index)->getIndexBuffer();
    };
    uint32_t getIndexCount(int model_index, int mesh_index)
    {
        return model_list[model_index]->getMesh(mesh_index)->getIndexCount();
    };
    uint32_t getNumberObjectDescriptions() { return static_cast<uint32_t>(object_descriptions.size()); };
    uint32_t getNumberMeshes();
    std::vector<ObjectDescription> getObjectDescriptions() { return object_descriptions; };
    std::vector<std::shared_ptr<Model>> const &get_model_list() { return model_list; };

    void loadModel(VulkanDevice *device, VkCommandPool commandPool);

    void add_model(std::shared_ptr<Model> model);
    void add_object_description(ObjectDescription object_description);

    void cleanUp();
    ~Scene();

  private:
    std::vector<ObjectDescription> object_descriptions;
    std::vector<std::shared_ptr<Model>> model_list;

    GUISceneSharedVars guiSceneSharedVars;
};
}// namespace Kataglyphis