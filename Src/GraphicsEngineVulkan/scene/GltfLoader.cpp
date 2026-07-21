module;

#include <cstddef>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cgltf.h>

module kataglyphis.vulkan.gltf_loader;

namespace Kataglyphis {

namespace {

/// Neutral Lambertian stand-in, used for primitives with no material. The
/// fields are ObjMaterial's; textureID -1 means untextured.
ObjMaterial neutralMaterial()
{
    return ObjMaterial(glm::vec3(0.1F),// ambient
      glm::vec3(0.8F),// diffuse
      glm::vec3(0.0F),// specular
      glm::vec3(0.0F),// transmittance
      glm::vec3(0.0F),// emission
      1.0F,// shininess
      1.0F,// ior
      1.0F,// dissolve
      2,// illum
      -1);// textureID
}

/// Maps a glTF material to the engine's ObjMaterial. Base-colour factor becomes
/// diffuse (the dominant term this forward renderer reads); ambient tracks it at
/// low strength. Metallic/roughness fold into a rough specular approximation -
/// this engine has no metallic-roughness PBR slot, so the mapping is lossy on
/// purpose. Textures are increment d; textureID stays -1 here.
ObjMaterial fromGltfMaterial(const cgltf_material &material)
{
    glm::vec3 baseColor(0.8F);
    float roughness = 1.0F;
    if (material.has_pbr_metallic_roughness != 0) {
        const cgltf_pbr_metallic_roughness &pbr = material.pbr_metallic_roughness;
        baseColor = glm::vec3(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]);
        roughness = pbr.roughness_factor;
    }
    const glm::vec3 emission(material.emissive_factor[0], material.emissive_factor[1], material.emissive_factor[2]);
    // Smoother surfaces (low roughness) get a tighter, stronger highlight.
    const float shininess = glm::mix(128.0F, 1.0F, glm::clamp(roughness, 0.0F, 1.0F));

    return ObjMaterial(baseColor * 0.1F,// ambient
      baseColor,// diffuse
      glm::vec3(1.0F - roughness) * 0.5F,// specular
      glm::vec3(0.0F),// transmittance
      emission,// emission
      shininess,// shininess
      1.0F,// ior
      1.0F,// dissolve
      2,// illum
      -1);// textureID (increment d)
}

/// Reads a float attribute (2 or 3 components) into `out`, one entry per accessor
/// element. cgltf handles the underlying component type and stride.
template<int N, typename VecT>
void readAttribute(const cgltf_accessor *accessor, std::vector<VecT> &out)
{
    out.resize(accessor->count);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
        cgltf_accessor_read_float(accessor, i, glm::value_ptr(out[i]), N);
    }
}

}// namespace

bool GltfLoader::parseCpu(const std::string &modelFile)
{
    vertices.clear();
    indices.clear();
    materials.clear();
    materialIndex.clear();

    cgltf_options options{};
    cgltf_data *data = nullptr;
    if (cgltf_parse_file(&options, modelFile.c_str(), &data) != cgltf_result_success) { return false; }
    if (cgltf_load_buffers(&options, data, modelFile.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        return false;
    }

    // One ObjMaterial per glTF material, plus a trailing neutral used by any
    // primitive that references none. `materialIndex` (per triangle) points into
    // this table.
    for (cgltf_size m = 0; m < data->materials_count; ++m) {
        materials.push_back(fromGltfMaterial(data->materials[m]));
    }
    const auto fallbackMaterial = static_cast<unsigned int>(materials.size());
    materials.push_back(neutralMaterial());

    // Walk the node hierarchy so each mesh instance carries its world transform
    // (glTF has nodes the OBJ path lacks; bake the transform into positions like
    // the Rust loader does). A mesh referenced by several nodes is emitted once
    // per node, matching glTF instancing semantics.
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node *node = &data->nodes[n];
        if (node->mesh == nullptr) { continue; }

        cgltf_float worldRaw[16];
        cgltf_node_transform_world(node, worldRaw);
        const glm::mat4 world = glm::make_mat4(worldRaw);
        const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(world));

        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
            const cgltf_primitive *primitive = &node->mesh->primitives[p];
            if (primitive->type != cgltf_primitive_type_triangles) { continue; }

            std::vector<glm::vec3> positions;
            std::vector<glm::vec3> normals;
            std::vector<glm::vec2> uvs;

            for (cgltf_size a = 0; a < primitive->attributes_count; ++a) {
                const cgltf_attribute *attribute = &primitive->attributes[a];
                switch (attribute->type) {
                case cgltf_attribute_type_position:
                    readAttribute<3>(attribute->data, positions);
                    break;
                case cgltf_attribute_type_normal:
                    readAttribute<3>(attribute->data, normals);
                    break;
                case cgltf_attribute_type_texcoord:
                    if (attribute->index == 0) { readAttribute<2>(attribute->data, uvs); }
                    break;
                default:
                    break;
                }
            }
            if (positions.empty()) { continue; }

            const auto base = static_cast<unsigned int>(vertices.size());
            for (std::size_t i = 0; i < positions.size(); ++i) {
                const glm::vec3 worldPos = glm::vec3(world * glm::vec4(positions[i], 1.0F));
                const glm::vec3 worldNormal =
                  i < normals.size() ? glm::normalize(normalMatrix * normals[i]) : glm::vec3(0.0F, 1.0F, 0.0F);
                const glm::vec2 uv = i < uvs.size() ? uvs[i] : glm::vec2(0.0F);
                vertices.emplace_back(worldPos, worldNormal, glm::vec3(1.0F), uv);
            }

            if (primitive->indices != nullptr) {
                const cgltf_accessor *idx = primitive->indices;
                for (cgltf_size i = 0; i < idx->count; ++i) {
                    indices.push_back(base + static_cast<unsigned int>(cgltf_accessor_read_index(idx, i)));
                }
            } else {
                // Non-indexed primitive: the vertices are the triangle sequence.
                for (std::size_t i = 0; i < positions.size(); ++i) {
                    indices.push_back(base + static_cast<unsigned int>(i));
                }
            }

            // One material id per triangle of this primitive (materialIndex is
            // per-face, like the OBJ path). All of a primitive's triangles share
            // its material.
            const unsigned int primitiveMaterial =
              primitive->material != nullptr
                ? static_cast<unsigned int>(primitive->material - data->materials)
                : fallbackMaterial;
            const std::size_t primitiveIndexCount =
              primitive->indices != nullptr ? primitive->indices->count : positions.size();
            materialIndex.insert(materialIndex.end(), primitiveIndexCount / 3, primitiveMaterial);
        }
    }

    cgltf_free(data);

    return !vertices.empty();
}

}// namespace Kataglyphis
