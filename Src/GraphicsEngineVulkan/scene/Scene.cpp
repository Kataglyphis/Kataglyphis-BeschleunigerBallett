#include <utility>

#include "ObjLoader.hpp"
#include "gui/GUI.hpp"
#include "scene/Model.hpp"
#include "scene/ObjectDescription.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneConfig.hpp"
#include "spdlog/spdlog.h"
#include "vulkan_base/VulkanDevice.hpp"
#include <cstdint>
#include <glm/ext/matrix_float4x4.hpp>
#include <memory>
#include <string>
#include <vulkan/vulkan_core.h>

using namespace Kataglyphis;

Scene::Scene() = default;

void Scene::update_user_input(Kataglyphis::Frontend::GUI *gui) { guiSceneSharedVars = gui->getGuiSceneSharedVars(); }

void Scene::loadModel(VulkanDevice *device, VkCommandPool commandPool)
{
    ObjLoader obj_loader(device, device->getGraphicsQueue(), commandPool);

    std::string const modelFileName = sceneConfig::getModelFile();
    std::shared_ptr<Model> const new_model = obj_loader.loadModel(modelFileName);

    add_model(new_model);

    glm::mat4 const modelMatrix = sceneConfig::getModelMatrix();

    update_model_matrix(modelMatrix, 0);
}

void Scene::add_model(const std::shared_ptr<Model> &model)
{
    model_list.push_back(model);
    object_descriptions.push_back(model->getObjectDescription());
}

void Scene::add_object_description(ObjectDescription object_description)
{
    object_descriptions.push_back(object_description);
}

void Scene::update_model_matrix(glm::mat4 model_matrix, int model_id)
{
    if (model_id < 0 || std::cmp_greater_equal(model_id, getModelCount())) {
        spdlog::error("Wrong model id value!");
        return;
    }

    model_list[model_id]->set_model(model_matrix);
}

void Scene::cleanUp()
{
    for (std::shared_ptr<Model> const &model : model_list) { model->cleanUp(); }
}

auto Scene::getNumberMeshes() -> uint32_t
{
    uint32_t number_of_meshes = 0;

    for (std::shared_ptr<Model> const &mesh_model : model_list) {
        number_of_meshes += mesh_model->getMeshCount();
    }

    return number_of_meshes;
}

Scene::~Scene() = default;
