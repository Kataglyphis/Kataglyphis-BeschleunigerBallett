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

    /// CPU-only construction, for parsing off the render thread (or in a test).
    /// loadModel() is unavailable on such an instance - it has no device to
    /// upload with.
    ObjLoader() = default;

    std::shared_ptr<Model> loadModel(const std::string &modelFile);

    /// Reads and decodes the OBJ into CPU-side vertex/index/material arrays.
    ///
    /// Touches NO Vulkan, which is the point: this is ~99% of model load time
    /// (measured 1906 ms parse + 867 ms vertex build against 15 ms of GPU
    /// upload on the bundled 27 MB model), so it is the part worth moving to a
    /// worker thread. Returns false on a malformed or missing file, leaving
    /// the instance empty.
    bool parseCpu(const std::string &modelFile);

    /// Results of the last parseCpu / loadModel. Exposed so a worker can hand
    /// the data to the thread that owns the device.
    const std::vector<Vertex> &getVertices() const { return vertices; }
    const std::vector<unsigned int> &getIndices() const { return indices; }
    const std::vector<ObjMaterial> &getMaterials() const { return materials; }
    const std::vector<unsigned int> &getMaterialIndices() const { return materialIndex; }
    const std::vector<std::string> &getTextureNames() const { return textures; }

  private:
    std::shared_ptr<VulkanDevice> device;
    vk::Queue transfer_queue;
    vk::CommandPool command_pool;

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<ObjMaterial> materials;
    std::vector<unsigned int> materialIndex;
    std::vector<std::string> textures;
    std::vector<std::string> textureNamesFromLastParse;

    // Both take an ALREADY PARSED reader. The file used to be parsed once per
    // function - twice per model - which for the bundled 27 MB OBJ is around
    // 190 ms of duplicated work at the measured ~7 ms/MB (BM_ObjParse_Suzanne).
    // modelFile is still needed: texture paths resolve relative to it.
    std::vector<std::string> loadTexturesAndMaterials(const tinyobj::ObjReader &reader, const std::string &modelFile);
    void loadVertices(const tinyobj::ObjReader &reader);
};
}// namespace Kataglyphis
