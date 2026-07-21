module;

#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.gltf_loader;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.model;
import kataglyphis.vulkan.obj_material;
import kataglyphis.vulkan.vertex;

export namespace Kataglyphis {

/// Loads `.gltf`/`.glb` geometry into the SAME CPU-side arrays `ObjLoader`
/// produces, so it plugs into `Model`/`Mesh` and the `AsyncModelParse` worker
/// unchanged. Touches no Vulkan - that is the point, the parse is the expensive
/// part worth moving off the render thread.
///
/// Increment b: geometry (positions/normals/UVs, indices, baked node
/// transforms) plus a single neutral material. Per-material import and texture
/// resolution are follow-ups (see BACKLOG glTF increments c/d).
class GltfLoader
{
  public:
    /// Device-owning: `loadModel` can upload. `parseCpu` alone needs no device,
    /// so the default constructor stays available for the CPU-only path.
    GltfLoader(std::shared_ptr<VulkanDevice> device, vk::Queue transfer_queue, vk::CommandPool command_pool);
    GltfLoader() = default;

    /// Parses `modelFile` then builds the Vulkan-side Model from it (parse +
    /// upload). Returns nullptr on a device-free loader or a failed parse.
    /// Every mesh uses the default texture for now - glTF texture import is
    /// increment d.
    std::shared_ptr<Model> loadModel(const std::string &modelFile);

    /// Parses the document into vertices/indices/materials/materialIndex.
    /// Returns false on a missing or malformed file, leaving the instance empty.
    bool parseCpu(const std::string &modelFile);

    const std::vector<Vertex> &getVertices() const { return vertices; }
    const std::vector<unsigned int> &getIndices() const { return indices; }
    const std::vector<ObjMaterial> &getMaterials() const { return materials; }
    const std::vector<unsigned int> &getMaterialIndices() const { return materialIndex; }

  private:
    std::shared_ptr<VulkanDevice> device;
    vk::Queue transfer_queue;
    vk::CommandPool command_pool;

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<ObjMaterial> materials;
    // Per-face (per-triangle) material id, exactly like the OBJ path.
    std::vector<unsigned int> materialIndex;
};

}// namespace Kataglyphis
