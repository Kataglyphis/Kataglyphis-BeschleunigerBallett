#include "scene/Scene.hpp"
#include "ObjLoader.hpp"
#include "common/Utilities.hpp"
#include "spdlog/spdlog.h"

using namespace Kataglyphis;

Scene::Scene() {}

void Scene::update_user_input(Kataglyphis::Frontend::GUI *gui) { guiSceneSharedVars = gui->getGuiSceneSharedVars(); }

void Scene::loadModel(VulkanDevice *device, VkCommandPool commandPool)
{
    ObjLoader obj_loader(device, device->getGraphicsQueue(), commandPool);

    std::string modelFileName = sceneConfig::getModelFile();
    std::shared_ptr<Model> new_model = obj_loader.loadModel(modelFileName);

    add_model(new_model);

    glm::mat4 modelMatrix = sceneConfig::getModelMatrix();

    update_model_matrix(modelMatrix, 0);
}

void Scene::add_model(std::shared_ptr<Model> model)
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
    if (model_id >= static_cast<int32_t>(getModelCount()) || model_id < 0) { spdlog::error("Wrong model id value!"); }

    model_list[model_id]->set_model(model_matrix);
}

void Scene::cleanUp()
{
    for (std::shared_ptr<Model> model : model_list) { model->cleanUp(); }
}

uint32_t Scene::getNumberMeshes()
{
    uint32_t number_of_meshes = 0;

    for (std::shared_ptr<Model> mesh_model : model_list) {
        number_of_meshes += static_cast<uint32_t>(mesh_model->getMeshCount());
    }

    return number_of_meshes;
}

Scene::~Scene() {}
