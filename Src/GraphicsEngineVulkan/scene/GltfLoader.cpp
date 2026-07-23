module;

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "spdlog/spdlog.h"

#include <cgltf.h>

module kataglyphis.vulkan.gltf_loader;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.model;
import kataglyphis.vulkan.texture;

namespace Kataglyphis {

GltfLoader::GltfLoader(std::shared_ptr<VulkanDevice> device, vk::Queue transfer_queue, vk::CommandPool command_pool)
  : device(std::move(device)), transfer_queue(transfer_queue), command_pool(command_pool)
{}

std::shared_ptr<Model> GltfLoader::loadModel(const std::string &modelFile)
{
    if (!parseCpu(modelFile)) {
        spdlog::error("Failed to parse glTF: {}", modelFile);
        return nullptr;
    }
    return uploadParsed();
}

void GltfLoader::adoptParsed(GltfLoader &&other)
{
    vertices = std::move(other.vertices);
    indices = std::move(other.indices);
    materials = std::move(other.materials);
    materialIndex = std::move(other.materialIndex);
    textureImages = std::move(other.textureImages);
    meshRanges = std::move(other.meshRanges);
}

std::shared_ptr<Model> GltfLoader::uploadParsed()
{
    if (!device) {
        spdlog::error("GltfLoader::uploadParsed called on a device-free loader");
        return nullptr;
    }
    if (vertices.empty()) {
        spdlog::error("GltfLoader::uploadParsed called before a successful parseCpu");
        return nullptr;
    }

    std::shared_ptr<Model> model = std::make_shared<Model>(device);

    // Decode + upload each recorded base-colour image (increment d); textureID
    // in each material indexes into these, added in the same order. If a
    // document had no textures, reserve the default so textureID 0 is valid -
    // matching the OBJ path.
    for (const std::vector<unsigned char> &encoded : textureImages) {
        Texture texture;
        if (texture.createFromMemory(device, command_pool, encoded.data(), encoded.size())) {
            model->addTexture(std::move(texture));
        } else {
            // Decode failed (e.g. a corrupt embedded image). Occupy this slot
            // with the default texture instead of skipping it: each material's
            // textureID is a dense index into textureImages, so a skipped slot
            // would shift every later texture down one and make the final
            // textureID index past the descriptor array.
            Texture defaultTexture;
            defaultTexture.createDefaultTexture(device, command_pool);
            model->addTexture(std::move(defaultTexture));
        }
    }
    if (model->getTextureCount() == 0) {
        Texture defaultTexture;
        defaultTexture.createDefaultTexture(device, command_pool);
        model->addTexture(std::move(defaultTexture));
    }

    // One Mesh per glTF primitive (backlog #10). Each range is that primitive's
    // slice of the flat arrays; a sub-mesh's indices are re-based to its own
    // vertex subset. A single-primitive glTF has exactly one range spanning
    // everything, so this builds one mesh - behaviour-identical to before. Each
    // mesh shares the full materials array (its materialIndex holds the original
    // indices); a per-mesh material subset is a later optimisation.
    if (meshRanges.empty()) {
        model->add_new_mesh(device, transfer_queue, command_pool, vertices, indices, materialIndex, materials);
    } else {
        for (const MeshRange &range : meshRanges) {
            std::vector<Vertex> subVertices(
              vertices.begin() + static_cast<std::ptrdiff_t>(range.vertexBase),
              vertices.begin() + static_cast<std::ptrdiff_t>(range.vertexBase + range.vertexCount));

            std::vector<unsigned int> subIndices;
            subIndices.reserve(range.indexCount);
            for (std::size_t i = 0; i < range.indexCount; ++i) {
                subIndices.push_back(indices[range.indexStart + i] - static_cast<unsigned int>(range.vertexBase));
            }

            std::vector<unsigned int> subMaterialIndex(
              materialIndex.begin() + static_cast<std::ptrdiff_t>(range.triStart),
              materialIndex.begin() + static_cast<std::ptrdiff_t>(range.triStart + range.triCount));

            model->add_new_mesh(
              device, transfer_queue, command_pool, subVertices, subIndices, subMaterialIndex, materials, range.doubleSided);
        }
    }
    return model;
}

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

    // glTF alphaMode MASK -> the shader discards where base-colour alpha < cutoff
    // (cut-out foliage/decals). OPAQUE and BLEND map to -1 (never discard); real
    // BLEND compositing needs a sorted transparent pass that this engine does not
    // have yet, so BLEND currently renders opaque - MASK is the common cut-out case.
    const float alphaCutoff = (material.alpha_mode == cgltf_alpha_mode_mask) ? material.alpha_cutoff : -1.0F;

    // glTF KHR_texture_transform on the base-colour texture: scale + offset the
    // UV (rotation is not yet applied - scale/offset is the common atlas/tiling
    // case). Absent -> identity (1,1)/(0,0), so untransformed materials are
    // bit-unchanged.
    glm::vec2 uvScale(1.0F, 1.0F);
    glm::vec2 uvOffset(0.0F, 0.0F);
    if (material.has_pbr_metallic_roughness != 0 && material.pbr_metallic_roughness.base_color_texture.has_transform != 0) {
        const cgltf_texture_transform &transform = material.pbr_metallic_roughness.base_color_texture.transform;
        uvScale = glm::vec2(transform.scale[0], transform.scale[1]);
        uvOffset = glm::vec2(transform.offset[0], transform.offset[1]);
    }

    return ObjMaterial(baseColor * 0.1F,// ambient
      baseColor,// diffuse
      glm::vec3(1.0F - roughness) * 0.5F,// specular
      glm::vec3(0.0F),// transmittance
      emission,// emission
      shininess,// shininess
      1.0F,// ior
      1.0F,// dissolve
      2,// illum
      -1,// textureID (increment d)
      alphaCutoff,// glTF MASK cutoff (-1 = OPAQUE/BLEND)
      uvScale,// KHR_texture_transform scale
      uvOffset);// KHR_texture_transform offset
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

/// Returns the ENCODED image bytes (PNG/JPG/...) for a glTF image, or empty if
/// unavailable. Handles the two forms every testable asset uses: glb buffer-view
/// embedded (bytes in a loaded buffer) and a base64 data-URI (cgltf decodes it).
/// External file URIs are not handled - no asset in the tree uses one, and it
/// would need the document path to resolve.
std::vector<unsigned char> extractImageBytes(const cgltf_image *image, const cgltf_options &options)
{
    if (image == nullptr) { return {}; }

    if (image->buffer_view != nullptr && image->buffer_view->buffer != nullptr
        && image->buffer_view->buffer->data != nullptr) {
        const auto *base = static_cast<const unsigned char *>(image->buffer_view->buffer->data);
        const cgltf_size offset = image->buffer_view->offset;
        const cgltf_size size = image->buffer_view->size;
        // A malformed file can declare a buffer view that runs past its
        // buffer; slicing base+offset..base+offset+size then reads OOB. Reject
        // any view that does not fit (offset+size guarded against overflow).
        const cgltf_size buffer_size = image->buffer_view->buffer->size;
        if (offset > buffer_size || size > buffer_size - offset) { return {}; }
        return std::vector<unsigned char>(base + offset, base + offset + size);
    }

    if (image->uri != nullptr) {
        const std::string uri = image->uri;
        const std::string marker = "base64,";
        const std::string::size_type pos = uri.find(marker);
        if (pos != std::string::npos) {
            const char *b64 = uri.c_str() + pos + marker.size();
            const cgltf_size b64len = uri.size() - pos - marker.size();
            // A valid base64 payload is a positive multiple of 4. Anything
            // shorter made (b64len/4)*3 - padding UNDERFLOW (cgltf_size is
            // unsigned), producing a ~SIZE_MAX allocation/read request from a
            // one-character URI. Reject non-conforming lengths outright.
            if (b64len < 4 || (b64len % 4) != 0) { return {}; }
            cgltf_size padding = 0;
            if (b64[b64len - 1] == '=') { ++padding; }
            if (b64[b64len - 2] == '=') { ++padding; }
            const cgltf_size decoded = (b64len / 4) * 3 - padding;
            void *out = nullptr;
            if (cgltf_load_buffer_base64(&options, decoded, b64, &out) == cgltf_result_success && out != nullptr) {
                const auto *bytes = static_cast<const unsigned char *>(out);
                std::vector<unsigned char> result(bytes, bytes + decoded);
                free(out);// cgltf's default allocator is malloc/free
                return result;
            }
        }
    }
    return {};
}

}// namespace

void GltfLoader::processPrimitive(const cgltf_primitive *primitive,
  const glm::mat4 &world,
  const glm::mat3 &normalMatrix,
  const cgltf_data *data,
  unsigned int fallbackMaterial)
{
    // Triangles, triangle strips and triangle fans are all supported
    // (strips/fans are triangulated into a list below). Points and
    // lines are not drawable in this triangle-only renderer, so they
    // are skipped - but a strip/fan used to be skipped too, silently
    // dropping whole meshes exported that way.
    const cgltf_primitive_type primType = primitive->type;
    if (primType != cgltf_primitive_type_triangles && primType != cgltf_primitive_type_triangle_strip
        && primType != cgltf_primitive_type_triangle_fan) {
        return;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> colors;

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
        case cgltf_attribute_type_color:
            // COLOR_0 multiplies the base colour (glTF spec). The file may
            // store it as vec3 or vec4; cgltf_accessor_read_float hands back
            // the rgb either way. Absent -> white below, so the shader
            // multiply is a no-op for the common uncoloured mesh.
            if (attribute->index == 0) { readAttribute<3>(attribute->data, colors); }
            break;
        default:
            break;
        }
    }
    if (positions.empty()) { return; }

    const auto base = static_cast<unsigned int>(vertices.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const glm::vec3 worldPos = glm::vec3(world * glm::vec4(positions[i], 1.0F));
        // A missing NORMAL used to become a constant (0,1,0), so a
        // normal-less glTF lit as if every face pointed up. The spec
        // requires computing flat normals; done below once this
        // primitive's index list is known. Placeholder for now.
        const glm::vec3 worldNormal =
          i < normals.size() ? glm::normalize(normalMatrix * normals[i]) : glm::vec3(0.0F, 1.0F, 0.0F);
        const glm::vec2 uv = i < uvs.size() ? uvs[i] : glm::vec2(0.0F);
        const glm::vec3 vcolor = i < colors.size() ? colors[i] : glm::vec3(1.0F);
        vertices.emplace_back(worldPos, worldNormal, vcolor, uv);
    }

    // Gather the primitive's local index sequence (from the accessor,
    // or the implicit 0..N-1 for a non-indexed primitive), then emit a
    // triangle LIST into the global buffer - triangulating strips and
    // fans as we go.
    std::vector<unsigned int> localSeq;
    if (primitive->indices != nullptr) {
        const cgltf_accessor *idx = primitive->indices;
        localSeq.reserve(idx->count);
        for (cgltf_size i = 0; i < idx->count; ++i) {
            localSeq.push_back(static_cast<unsigned int>(cgltf_accessor_read_index(idx, i)));
        }
    } else {
        localSeq.reserve(positions.size());
        for (std::size_t i = 0; i < positions.size(); ++i) {
            localSeq.push_back(static_cast<unsigned int>(i));
        }
    }

    const std::size_t primIndexStart = indices.size();
    const auto emitTri = [&](unsigned int a, unsigned int b, unsigned int c) {
        indices.push_back(base + a);
        indices.push_back(base + b);
        indices.push_back(base + c);
    };
    if (primType == cgltf_primitive_type_triangle_strip) {
        // Strip: alternate winding each step to keep a consistent
        // front face (glTF/OpenGL convention).
        for (std::size_t i = 0; i + 2 < localSeq.size(); ++i) {
            if ((i & 1U) == 0U) {
                emitTri(localSeq[i], localSeq[i + 1], localSeq[i + 2]);
            } else {
                emitTri(localSeq[i + 1], localSeq[i], localSeq[i + 2]);
            }
        }
    } else if (primType == cgltf_primitive_type_triangle_fan) {
        // Fan: vertex 0 is shared by every triangle.
        for (std::size_t i = 1; i + 1 < localSeq.size(); ++i) {
            emitTri(localSeq[0], localSeq[i], localSeq[i + 1]);
        }
    } else {
        for (std::size_t i = 0; i + 2 < localSeq.size(); i += 3) {
            emitTri(localSeq[i], localSeq[i + 1], localSeq[i + 2]);
        }
    }

    // Flat normals when NORMAL is absent (glTF spec: implementations
    // MUST compute them). Per-triangle geometric normal from the
    // already-world-space vertex positions, assigned to each of the
    // triangle's vertices - the same flat approximation the OBJ path
    // uses. Only runs for primitives that shipped no normals.
    if (normals.empty()) {
        for (std::size_t i = primIndexStart; i + 2 < indices.size(); i += 3) {
            Vertex &v0 = vertices[indices[i + 0]];
            Vertex &v1 = vertices[indices[i + 1]];
            Vertex &v2 = vertices[indices[i + 2]];
            const glm::vec3 edge1 = v1.position - v0.position;
            const glm::vec3 edge2 = v2.position - v0.position;
            const glm::vec3 faceNormal = glm::cross(edge1, edge2);
            // Degenerate triangle (zero area): leave the up default
            // rather than emit a NaN from normalizing a zero vector.
            if (glm::dot(faceNormal, faceNormal) <= 0.0F) { continue; }
            const glm::vec3 n = glm::normalize(faceNormal);
            v0.normal = n;
            v1.normal = n;
            v2.normal = n;
        }
    }

    // One material id per triangle of this primitive (materialIndex is
    // per-face, like the OBJ path). All of a primitive's triangles share
    // its material.
    const unsigned int primitiveMaterial =
      primitive->material != nullptr
        ? static_cast<unsigned int>(primitive->material - data->materials)
        : fallbackMaterial;
    // One id per EMITTED triangle - after triangulation, not from the
    // raw index count (which for a strip/fan over-counts by ~3x).
    const std::size_t triStart = materialIndex.size();
    const std::size_t emittedTriangles = (indices.size() - primIndexStart) / 3;
    materialIndex.insert(materialIndex.end(), emittedTriangles, primitiveMaterial);

    // This primitive's slice of the flat arrays, so uploadParsed can build
    // it as its own Mesh (backlog #10). The union of all ranges is exactly
    // the flat arrays, so a single-primitive glTF yields one range and is
    // behaviour-identical.
    const bool doubleSided = primitive->material != nullptr && primitive->material->double_sided != 0;
    meshRanges.push_back(GltfLoader::MeshRange{ static_cast<std::size_t>(base),
      positions.size(),
      primIndexStart,
      indices.size() - primIndexStart,
      triStart,
      emittedTriangles,
      doubleSided });
}

bool GltfLoader::parseCpu(const std::string &modelFile)
{
    vertices.clear();
    indices.clear();
    materials.clear();
    materialIndex.clear();
    textureImages.clear();

    cgltf_options options{};
    cgltf_data *data = nullptr;
    if (cgltf_parse_file(&options, modelFile.c_str(), &data) != cgltf_result_success) { return false; }
    // Structural validation before we walk the document: cgltf_parse checks
    // JSON well-formedness, cgltf_validate checks that counts, indices and
    // references are internally consistent. The GUI feeds arbitrary user
    // files here, and the walk below trusts these invariants.
    if (cgltf_validate(data) != cgltf_result_success) {
        cgltf_free(data);
        return false;
    }
    if (cgltf_load_buffers(&options, data, modelFile.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        return false;
    }

    // One ObjMaterial per glTF material, plus a trailing neutral used by any
    // primitive that references none. `materialIndex` (per triangle) points into
    // this table.
    for (cgltf_size m = 0; m < data->materials_count; ++m) {
        const cgltf_material &material = data->materials[m];
        ObjMaterial objMaterial = fromGltfMaterial(material);
        // A base-colour texture (per material, like the OBJ path): record its
        // encoded bytes and point the material at the new texture slot.
        if (material.has_pbr_metallic_roughness != 0
            && material.pbr_metallic_roughness.base_color_texture.texture != nullptr) {
            std::vector<unsigned char> bytes =
              extractImageBytes(material.pbr_metallic_roughness.base_color_texture.texture->image, options);
            if (!bytes.empty()) {
                objMaterial.textureID = static_cast<int>(textureImages.size());
                textureImages.push_back(std::move(bytes));
            }
        }
        materials.push_back(objMaterial);
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
            processPrimitive(&node->mesh->primitives[p], world, normalMatrix, data, fallbackMaterial);
        }
    }

    cgltf_free(data);

    return !vertices.empty();
}

}// namespace Kataglyphis
