module;
#include <optional>

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
import kataglyphis.vulkan.gltf_loader;
import kataglyphis.vulkan.model;
import kataglyphis.vulkan.device;
import kataglyphis.vulkan.object_description;
import kataglyphis.vulkan.scene_config;
import kataglyphis.vulkan.model_file_kind;

namespace Kataglyphis {

namespace {

/// Picks the loader by file extension: glTF documents (`.gltf`/`.glb`) go to
/// GltfLoader, everything else to ObjLoader. Both produce a device-side Model
/// the same way, so callers do not care which ran.
std::shared_ptr<Model> loadModelByExtension(std::shared_ptr<VulkanDevice> device,
  vk::CommandPool commandPool,
  const std::string &modelFile)
{
    if (isGltfModelPath(modelFile)) {
        GltfLoader gltf_loader(device, commandPool);
        return gltf_loader.loadModel(modelFile);
    }

    ObjLoader obj_loader(device, commandPool);
    return obj_loader.loadModel(modelFile);
}

}// namespace

Scene::Scene() = default;

void Scene::loadModel(std::shared_ptr<VulkanDevice>device, vk::CommandPool commandPool)
{
    std::string const modelFileName = sceneConfig::getModelFile();
    spdlog::info("Loading model: {}", modelFileName);
    std::shared_ptr<Model> const new_model = loadModelByExtension(device, commandPool, modelFileName);

    if (new_model) {
        const std::optional<uint32_t> model_index = add_model(new_model);
        spdlog::info("Model added successfully.");
        if (model_index) {
            glm::mat4 const modelMatrix = sceneConfig::getModelMatrix();
            update_model_matrix(modelMatrix, *model_index);
        }
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
    // Resolve like loadModel() does: callers pass a path relative to
    // Resources/, and the working directory differs between the app and the
    // test executables.
    const std::string resolved = sceneConfig::resolveModelPath(modelPath);
    spdlog::info("Loading additional model: {}", resolved);
    std::shared_ptr<Model> const new_model = loadModelByExtension(device, commandPool, resolved);
    if (!new_model) {
        spdlog::error("Failed to load additional model: {}", modelPath);
        return std::nullopt;
    }

    const std::optional<uint32_t> model_index = add_model(new_model);
    if (!model_index) { return std::nullopt; }
    update_model_matrix(modelMatrix, *model_index);
    spdlog::info("Additional model added at index {}.", *model_index);
    return model_index;
}

void Scene::beginModelLoadAsync()
{
    const std::string modelFileName = sceneConfig::getModelFile();
    spdlog::info("Loading model asynchronously: {}", modelFileName);
    pendingModelParse.start(modelFileName);
    modelLoadPending = true;
}

bool Scene::isModelLoadPending() const { return modelLoadPending; }

bool Scene::pollModelLoad(std::shared_ptr<VulkanDevice> device, vk::CommandPool commandPool)
{
    if (!modelLoadPending || !pendingModelParse.isFinished()) { return false; }

    modelLoadPending = false;

    // The GPU half runs HERE, on the thread that owns the device. It is the
    // ~15 ms the frame loop still pays; the 2800 ms parse already happened on
    // the worker. Dispatch by which loader the worker used (glTF vs OBJ); both
    // now expose adoptParsed + uploadParsed, so the two arms are symmetric.
    std::shared_ptr<Model> new_model;
    if (pendingModelParse.parsedGltf()) {
        std::unique_ptr<GltfLoader> parsed = pendingModelParse.takeGltfResult();
        if (!parsed) {
            spdlog::error("Asynchronous glTF parse failed; the scene stays empty.");
            return false;
        }
        GltfLoader uploader(device, commandPool);
        uploader.adoptParsed(std::move(*parsed));
        new_model = uploader.uploadParsed();
    } else {
        std::unique_ptr<ObjLoader> parsed = pendingModelParse.takeResult();
        if (!parsed) {
            spdlog::error("Asynchronous model parse failed; the scene stays empty.");
            return false;
        }
        ObjLoader uploader(device, commandPool);
        uploader.adoptParsed(std::move(*parsed));
        new_model = uploader.uploadParsed();
    }
    if (!new_model) {
        spdlog::error("Uploading the parsed model failed.");
        return false;
    }

    const std::optional<uint32_t> model_index = add_model(new_model);
    if (model_index) { update_model_matrix(sceneConfig::getModelMatrix(), *model_index); }
    spdlog::info("Model added successfully (async).");
    return true;
}

std::optional<uint32_t> Scene::add_model(const std::shared_ptr<Model> &model)
{
    // Loaders return nullptr for malformed assets; dereferencing it here
    // turned a bad file from the GUI picker into a crash.
    if (!model) {
        spdlog::error("add_model called with a null model - a load failed upstream; scene unchanged.");
        return std::nullopt;
    }
    model_list.push_back(model);
    // One object description per MESH, flattened across models: objectIndex (the
    // per-draw push constant) indexes this list.
    for (uint32_t k = 0; k < model->getMeshCount(); ++k) {
        object_descriptions.push_back(model->getMesh(k)->getObjectDescription());
    }
    return getModelCount() - 1U;
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

    // By extension, like every other load path - reloadModel constructed
    // ObjLoader directly, so picking a .glb from the GUI fed glTF bytes to
    // the OBJ parser.
    std::shared_ptr<Model> const new_model = loadModelByExtension(device, commandPool, modelPath);
    const std::optional<uint32_t> model_index = add_model(new_model);
    if (!model_index) {
        spdlog::error("reloadModel: '{}' failed to load; scene is now empty.", modelPath);
        return;
    }

    glm::mat4 const modelMatrix = sceneConfig::getModelMatrix();
    update_model_matrix(modelMatrix, *model_index);
}

void Scene::cleanUp()
{
    for (std::shared_ptr<Model> const &model : model_list) { model->cleanUp(); }
}

Scene::~Scene() { cleanUp(); }

}// namespace Kataglyphis
