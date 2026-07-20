module;
#include <optional>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/matrix_float4x4.hpp>
#include <vulkan/vulkan.hpp>

#include "spdlog/spdlog.h"

module kataglyphis.vulkan.scene;

import kataglyphis.vulkan.obj_loader;
import kataglyphis.vulkan.model;
import kataglyphis.vulkan.device;
import kataglyphis.vulkan.gui;
import kataglyphis.vulkan.object_description;
import kataglyphis.vulkan.scene_config;

namespace Kataglyphis {

Scene::Scene() = default;

void Scene::update_user_input(Kataglyphis::Frontend::GUI *gui) { guiSceneSharedVars = gui->getGuiSceneSharedVars(); }

void Scene::loadModel(std::shared_ptr<VulkanDevice>device, vk::CommandPool commandPool)
{
    ObjLoader obj_loader(device, device->getGraphicsQueue(), commandPool);

    std::string const modelFileName = sceneConfig::getModelFile();
    spdlog::info("Loading model: {}", modelFileName);
    std::shared_ptr<Model> const new_model = obj_loader.loadModel(modelFileName);
    
    if (new_model) {
        add_model(new_model);
        spdlog::info("Model added successfully.");
        glm::mat4 const modelMatrix = sceneConfig::getModelMatrix();
        update_model_matrix(modelMatrix, 0);
    } else {
        spdlog::error("Failed to load model: {}", modelFileName);
        return;
    }
}

std::optional<uint32_t> Scene::loadAdditionalModel(std::shared_ptr<VulkanDevice> device,
  vk::CommandPool commandPool,
  const std::string &modelPath,
  const glm::mat4 &modelMatrix)
{
    ObjLoader obj_loader(device, device->getGraphicsQueue(), commandPool);

    // Resolve like loadModel() does: callers pass a path relative to
    // Resources/, and the working directory differs between the app and the
    // test executables.
    const std::string resolved = sceneConfig::resolveModelPath(modelPath);
    spdlog::info("Loading additional model: {}", resolved);
    std::shared_ptr<Model> const new_model = obj_loader.loadModel(resolved);
    if (!new_model) {
        spdlog::error("Failed to load additional model: {}", modelPath);
        return std::nullopt;
    }

    add_model(new_model);
    const uint32_t model_index = getModelCount() - 1U;
    update_model_matrix(modelMatrix, model_index);
    spdlog::info("Additional model added at index {}.", model_index);
    return model_index;
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

void Scene::update_model_matrix(glm::mat4 model_matrix, uint32_t model_id)
{
    if (model_id >= getModelCount()) {
        spdlog::error("Wrong model id value! model_id: {}, model_count: {}", model_id, getModelCount());
        return;
    }

    model_list[static_cast<size_t>(model_id)]->set_model(model_matrix);
}

void Scene::reloadModel(std::shared_ptr<VulkanDevice>device, vk::CommandPool commandPool, const std::string &modelPath)
{
    cleanUp();
    model_list.clear();
    object_descriptions.clear();

    ObjLoader obj_loader(device, device->getGraphicsQueue(), commandPool);
    std::shared_ptr<Model> const new_model = obj_loader.loadModel(modelPath);
    add_model(new_model);

    glm::mat4 const modelMatrix = sceneConfig::getModelMatrix();
    update_model_matrix(modelMatrix, 0);
}

void Scene::cleanUp()
{
    for (std::shared_ptr<Model> const &model : model_list) { model->cleanUp(); }
}

auto Scene::getNumberMeshes() -> uint32_t
{
    uint32_t number_of_meshes = 0;

    for (std::shared_ptr<Model> const &mesh_model : model_list) { number_of_meshes += mesh_model->getMeshCount(); }

    return number_of_meshes;
}

Scene::~Scene() { cleanUp(); }

}// namespace Kataglyphis
