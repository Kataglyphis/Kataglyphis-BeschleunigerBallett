#pragma once
#include <vulkan/vulkan.hpp>

#include <memory>

#include "Model.hpp"

import kataglyphis.vulkan.obj_material;
import kataglyphis.vulkan.vertex;

namespace Kataglyphis {
class ObjLoader
{
  public:
    ObjLoader(std::shared_ptr<VulkanDevice>device, vk::Queue transfer_queue, vk::CommandPool command_pool);

    std::shared_ptr<Model> loadModel(const std::string &modelFile);

  private:
    std::shared_ptr<VulkanDevice> device;
    vk::Queue transfer_queue;
    vk::CommandPool command_pool;

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<ObjMaterial> materials;
    std::vector<unsigned int> materialIndex;
    std::vector<std::string> textures;

    std::vector<std::string> loadTexturesAndMaterials(const std::string &modelFile);
    void loadVertices(const std::string &fileName);
};
}// namespace Kataglyphis