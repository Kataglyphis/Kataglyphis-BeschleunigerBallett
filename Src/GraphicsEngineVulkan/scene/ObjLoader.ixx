module;
#include <tiny_obj_loader.h>
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.obj_loader;

import kataglyphis.vulkan.obj_material;
import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.model;
import kataglyphis.vulkan.device;

export namespace Kataglyphis {
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

    // Both take an ALREADY PARSED reader. The file used to be parsed once per
    // function - twice per model - which for the bundled 27 MB OBJ is around
    // 190 ms of duplicated work at the measured ~7 ms/MB (BM_ObjParse_Suzanne).
    // modelFile is still needed: texture paths resolve relative to it.
    std::vector<std::string> loadTexturesAndMaterials(const tinyobj::ObjReader &reader, const std::string &modelFile);
    void loadVertices(const tinyobj::ObjReader &reader);
};
}// namespace Kataglyphis
