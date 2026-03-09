module;
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

export module kataglyphis.vulkan.obj_loader;

import kataglyphis.vulkan.obj_material;
import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.model;
import kataglyphis.vulkan.device;

export namespace Kataglyphis {
class ObjLoader
{
  public:
    ObjLoader(VulkanDevice *device, VkQueue transfer_queue, VkCommandPool command_pool);

    std::shared_ptr<Model> loadModel(const std::string &modelFile);

  private:
    Kataglyphis::VulkanDevice *device;
    VkQueue transfer_queue;
    VkCommandPool command_pool;

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<ObjMaterial> materials;
    std::vector<unsigned int> materialIndex;
    std::vector<std::string> textures;

    std::vector<std::string> loadTexturesAndMaterials(const std::string &modelFile);
    void loadVertices(const std::string &fileName);
};
}// namespace Kataglyphis
