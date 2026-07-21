module;

#include <string>
#include <vector>

export module kataglyphis.vulkan.gltf_loader;

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
    GltfLoader() = default;

    /// Parses the document into vertices/indices/materials/materialIndex.
    /// Returns false on a missing or malformed file, leaving the instance empty.
    bool parseCpu(const std::string &modelFile);

    const std::vector<Vertex> &getVertices() const { return vertices; }
    const std::vector<unsigned int> &getIndices() const { return indices; }
    const std::vector<ObjMaterial> &getMaterials() const { return materials; }
    const std::vector<unsigned int> &getMaterialIndices() const { return materialIndex; }

  private:
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<ObjMaterial> materials;
    // Per-face (per-triangle) material id, exactly like the OBJ path.
    std::vector<unsigned int> materialIndex;
};

}// namespace Kataglyphis
