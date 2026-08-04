module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>
#include "spdlog/spdlog.h"
#define TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_DISABLE_FAST_FLOAT
#include <glm/ext/vector_float2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <algorithm>
#include <iterator>
#include <iostream>
#include <tiny_obj_loader.h>
#include <unordered_map>

module kataglyphis.vulkan.obj_loader;

import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.device;
import kataglyphis.vulkan.obj_material;
import kataglyphis.vulkan.model;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.file;
import kataglyphis.vulkan.mesh_range;
import kataglyphis.vulkan.model_assembly;

using namespace Kataglyphis;

ObjLoader::ObjLoader(const std::shared_ptr<VulkanDevice> &device, vk::CommandPool command_pool)
  : device(device), command_pool(command_pool)
{}

bool ObjLoader::parseCpu(const std::string &modelFile)
{
    // clear prior state if called multiple times on the same instance
    textures.clear();
    materials.clear();
    vertices.clear();
    indices.clear();
    materialIndex.clear();
    meshRanges.clear();

    tinyobj::ObjReaderConfig const reader_config;
    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(modelFile, reader_config)) {
        // Must not kill the process: the GUI model picker can hand this
        // arbitrary files. loadVertices already returned gracefully here, but
        // loadTexturesAndMaterials called exit(EXIT_FAILURE) and ran first,
        // so a malformed asset took the application down regardless.
        if (!reader.Error().empty()) { spdlog::error("TinyObjReader: {}", reader.Error()); }
        return false;
    }
    if (!reader.Warning().empty()) { spdlog::warn("TinyObjReader: {}", reader.Warning()); }

    loadTexturesAndMaterials(reader, modelFile);
    loadVertices(reader);
    return true;
}

auto ObjLoader::loadModel(const std::string &modelFile) -> std::shared_ptr<Model>
{
    using clock = std::chrono::steady_clock;
    const auto load_started = clock::now();

    if (!parseCpu(modelFile)) { return nullptr; }

    const auto parse_done = clock::now();
    std::shared_ptr<Model> new_model = uploadParsed();
    const auto upload_done = clock::now();

    const auto ms = [](auto from, auto to) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
    };
    // Split at the boundary that matters: everything before parse_done is
    // device-free and can move to a worker; everything after must stay on the
    // thread that owns the device.
    spdlog::info(
      "Model load: CPU parse {} ms (threadable), GPU textures+upload {} ms (must stay on this thread), "
      "total {} ms ({} verts, {} indices)",
      ms(load_started, parse_done),
      ms(parse_done, upload_done),
      ms(load_started, upload_done),
      vertices.size(),
      indices.size());

    return new_model;
}

auto ObjLoader::uploadParsed() -> std::shared_ptr<Model>
{
    // GPU half. Must run on the thread that owns the device; parseCpu can run
    // anywhere.
    if (!device) {
        spdlog::error("uploadParsed called on a device-free ObjLoader");
        return nullptr;
    }
    if (vertices.empty()) {
        spdlog::error("uploadParsed called before a successful parseCpu");
        return nullptr;
    }

    std::shared_ptr<Model> new_model = std::make_shared<Model>(device);

    const std::vector<std::string> &textureNames = textures;

    // now that we have the names lets create the vulkan side of textures
    for (size_t i = 0; i < textureNames.size(); i++) {
        // If material had no texture, set '0' to indicate no texture, texture 0
        // will be reserved for a default texture
        if (!textureNames[i].empty()) {
            Texture texture;
            const bool created = texture.createFromFile(device, command_pool, textureNames[i]);
            addTextureOrDefault(*new_model, device, command_pool, created, std::move(texture));
        }
    }

    ensureAtLeastOneTexture(*new_model, device, command_pool);

    // One Mesh per OBJ shape (see MeshRange). Each mesh shares the full materials
    // array - its materialIndex still holds the original indices - matching the
    // glTF split; a per-mesh material subset is a later optimisation. A
    // single-shape OBJ (or the empty-range fallback) builds exactly one mesh, so
    // existing single-object models are behaviour-identical.
    addMeshesForRanges(
      *new_model, device, command_pool, vertices, indices, materialIndex, this->materials, meshRanges);
    return new_model;
}

std::string Kataglyphis::resolveObjTexturePath(const std::string &baseDir, const std::string &mapKd)
{
    // docs/model-loading.md: normalise `\` to `/` first - a Windows-authored
    // .mtl (`textures\wood.png`) must still resolve on platforms where `\`
    // is just another filename character, matching the Rust side's
    // parse_mtl (obj_to_gltf.rs).
    std::string normalized_map_kd = mapKd;
    std::replace(normalized_map_kd.begin(), normalized_map_kd.end(), '\\', '/');

    // An empty baseDir (a bare filename with no directory component) stays
    // relative rather than growing a leading "/" that resolves against the
    // filesystem root.
    const std::string base = baseDir.empty() ? "." : baseDir;

    const std::string beside_mtl = base + "/" + normalized_map_kd;
    const std::string under_textures = base + "/textures/" + normalized_map_kd;

    std::error_code beside_mtl_ec;
    if (std::filesystem::exists(beside_mtl, beside_mtl_ec) && !beside_mtl_ec) { return beside_mtl; }

    std::error_code under_textures_ec;
    if (std::filesystem::exists(under_textures, under_textures_ec) && !under_textures_ec) {
        return under_textures;
    }

    // Neither candidate exists. Keep today's behaviour - record a path and
    // let uploadParsed substitute the default texture - but warn loudly: a
    // wrong path degrading silently to white is the failure mode this whole
    // resolution rule is about.
    spdlog::warn(
      "map_Kd '{}' not found beside the .mtl ('{}') or under textures/ ('{}'); the model will "
      "render with the default texture",
      mapKd,
      beside_mtl,
      under_textures);
    return beside_mtl;
}

void ObjLoader::loadTexturesAndMaterials(const tinyobj::ObjReader &reader, const std::string &modelFile)
{
    const auto &tol_materials = reader.GetMaterials();
    textures.reserve(tol_materials.size());

    int texture_id = 0;
    File model_file(modelFile);
    const std::string base_dir = model_file.getBaseDir();

    // Materials whose diffuse texture resolves to the same on-disk path share
    // one textures slot: keyed on the resolved path (not the raw map_Kd), so
    // two .mtl spellings of the same file collapse together.
    std::unordered_map<std::string, int> pathSlot;

    // we now iterate over all materials to get diffuse textures
    for (const auto &tol_material : tol_materials) {
        const tinyobj::material_t *mp = &tol_material;
        ObjMaterial material{};
        material.diffuse = glm::vec3(mp->diffuse[0], mp->diffuse[1], mp->diffuse[2]);
        material.emission = glm::vec3(mp->emission[0], mp->emission[1], mp->emission[2]);
        material.dissolve = mp->dissolve;
        material.shininess = mp->shininess;

        if (!mp->diffuse_texname.empty()) {
            const std::string resolved = resolveObjTexturePath(base_dir, mp->diffuse_texname);
            const auto existing = pathSlot.find(resolved);
            if (existing != pathSlot.end()) {
                material.textureID = existing->second;
                // uploadParsed's dense counter walks textures 1:1 with
                // materials; an empty entry here keeps that mapping intact
                // while skipping the (already-uploaded) duplicate texture.
                textures.emplace_back("");
            } else {
                pathSlot[resolved] = texture_id;
                textures.push_back(resolved);
                material.textureID = texture_id;
                texture_id++;
            }

        } else {
            // No diffuse texture: -1 routes the shaders to material.diffuse.
            // This was 0, which sampled texture slot 0 instead - so a
            // texture-less .mtl (the bundled dinosaurs) rendered its Kd
            // colours as flat WHITE for as long as the engine existed.
            material.textureID = -1;
            textures.emplace_back("");
        }

        materials.push_back(material);
    }

    // for the case no .mtl file is given place some random standard material ...
    if (tol_materials.empty()) { materials.emplace_back(); }
}

void ObjLoader::loadVertices(const tinyobj::ObjReader &reader)
{
    const auto &attrib = reader.GetAttrib();
    const auto &shapes = reader.GetShapes();

    // indices ends up exactly this long. vertices is left to the per-shape
    // reserve below: reserving face-vertex count for it would hold ~39 MB for
    // a mesh that needs ~7 MB, since sharing is the normal case.
    size_t total_face_vertices = 0;
    for (const auto &shape : shapes) { total_face_vertices += shape.mesh.indices.size(); }
    indices.reserve(total_face_vertices);

    // Loop over shapes. Each shape (an OBJ `o`/`g` group) becomes its own Mesh
    // via the MeshRange recorded below, so the vertex-dedup map is per-shape:
    // that keeps every shape's vertices in a contiguous block uploadParsed can
    // slice, at the cost of duplicating any vertex shared across shapes (rare
    // between separate objects, and pixel-identical either way). The map is still
    // reserved to the shape's face-vertex upper bound to avoid rehash churn.
    for (const auto &shape : shapes) {
        std::unordered_map<Vertex, uint32_t> vertices_map{};
        vertices_map.reserve(shape.mesh.indices.size());

        const size_t shape_vertex_base = vertices.size();
        const size_t shape_index_start = indices.size();
        const size_t shape_tri_start = materialIndex.size();

        // prepare for enlargement
        vertices.reserve(shape.mesh.indices.size() + vertices.size());
        indices.reserve(shape.mesh.indices.size() + indices.size());

        // Loop over faces(polygon)
        size_t index_offset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            auto const fv = static_cast<size_t>(shape.mesh.num_face_vertices[f]);

            // Malformed files can carry negative or out-of-range indices
            // (fuzz-found hazard): validate every vertex of the face before
            // emitting any of it. materialIndex is one entry per emitted
            // triangle and indices must stay a multiple of 3, so dropping a
            // single corner would desynchronise the shaders'
            // materialIDs.i[prim] lookup as well as overrunning the array -
            // a malformed corner drops the whole face instead.
            bool face_valid = true;
            for (size_t v = 0; v < fv; v++) {
                tinyobj::index_t const idx = shape.mesh.indices[index_offset + v];
                const auto vertex_index = static_cast<size_t>(idx.vertex_index);
                if (idx.vertex_index < 0 || (3 * vertex_index) + 2 >= attrib.vertices.size()) {
                    face_valid = false;
                    break;
                }
            }

            if (face_valid) {
                // Loop over vertices in the face.
                for (size_t v = 0; v < fv; v++) {
                    // access to vertex
                    tinyobj::index_t const idx = shape.mesh.indices[index_offset + v];
                    const auto vertex_index = static_cast<size_t>(idx.vertex_index);
                    tinyobj::real_t const vx = attrib.vertices[(3 * vertex_index) + 0];
                    tinyobj::real_t const vy = attrib.vertices[(3 * vertex_index) + 1];
                    tinyobj::real_t const vz = attrib.vertices[(3 * vertex_index) + 2];
                    glm::vec3 const pos = { vx, vy, vz };

                    glm::vec3 normals(0.0F);
                    // Check if `normal_index` is zero or positive. negative = no normal
                    // data
                    if (idx.normal_index >= 0
                        && (3 * static_cast<size_t>(idx.normal_index)) + 2 < attrib.normals.size()) {
                        tinyobj::real_t const nx = attrib.normals[(3 * static_cast<size_t>(idx.normal_index)) + 0];
                        tinyobj::real_t const ny = attrib.normals[(3 * static_cast<size_t>(idx.normal_index)) + 1];
                        tinyobj::real_t const nz = attrib.normals[(3 * static_cast<size_t>(idx.normal_index)) + 2];
                        normals = glm::vec3(nx, ny, nz);
                    }

                    // White when the OBJ carries no per-vertex colour: the fragment
                    // shader multiplies this in (glTF COLOR_0 semantics, shared path),
                    // so the absent case must be the identity (1,1,1), not the old -1
                    // sentinel that would have darkened/inverted the surface. OBJ has
                    // no per-vertex alpha channel, so alpha is always 1.0 - the shared
                    // MASK alpha test's third factor is a glTF COLOR_0-only concept.
                    glm::vec4 color(1.F);
                    if ((3 * vertex_index) + 2 < attrib.colors.size()) {
                        tinyobj::real_t const red = attrib.colors[(3 * vertex_index) + 0];
                        tinyobj::real_t const green = attrib.colors[(3 * vertex_index) + 1];
                        tinyobj::real_t const blue = attrib.colors[(3 * vertex_index) + 2];
                        color = glm::vec4(red, green, blue, 1.F);
                    }

                    glm::vec2 tex_coords(0.0F);
                    // Check if `texcoord_index` is zero or positive. negative = no texcoord
                    // data
                    if (idx.texcoord_index >= 0
                        && (2 * static_cast<size_t>(idx.texcoord_index)) + 1 < attrib.texcoords.size()) {
                        tinyobj::real_t const tx =
                          attrib.texcoords[(2 * static_cast<size_t>(idx.texcoord_index)) + 0];
                        // flip y coordinate !!
                        tinyobj::real_t const ty =
                          1.F - attrib.texcoords[(2 * static_cast<size_t>(idx.texcoord_index)) + 1];
                        tex_coords = glm::vec2(tx, ty);
                    }

                    Vertex const vert{ pos, normals, color, tex_coords };

                    // ONE hash lookup per vertex. This was three - contains(),
                    // then operator[] to insert, then operator[] again to read -
                    // and at ~900k face vertices that is 1.8 million redundant
                    // hashes and probes.
                    const auto [entry, inserted] =
                      vertices_map.try_emplace(vert, static_cast<uint32_t>(vertices.size()));
                    if (inserted) { vertices.push_back(vert); }
                    indices.push_back(entry->second);
                }

                // Per-face material. tinyobj reports -1 for a face without a
                // material (any OBJ shipping no mtllib); the plain cast sent
                // 0xFFFFFFFF to the GPU, and every material fetch in the shaders
                // (materials.m[materialIDs.i[prim]]) became an OUT-OF-BOUNDS
                // buffer-device-address read. Route those faces to slot 0:
                // loadTexturesAndMaterials appends a default material when the
                // file ships none, and when it ships some, the first one is a
                // strictly better fallback than reading unmapped memory.
                const int face_material = shape.mesh.material_ids[f];
                materialIndex.push_back(face_material >= 0 ? static_cast<uint32_t>(face_material) : 0U);
            }

            index_offset += fv;
        }

        // Record this shape's contiguous slice so uploadParsed can build it as
        // its own Mesh. Skip a shape that emitted no geometry (every face fell to
        // the malformed-index guard) so uploadParsed never builds an empty mesh.
        if (vertices.size() > shape_vertex_base) {
            meshRanges.push_back(MeshRange{ shape_vertex_base,
                                                       vertices.size() - shape_vertex_base,
                                                       shape_index_start,
                                                       indices.size() - shape_index_start,
                                                       shape_tri_start,
                                                       materialIndex.size() - shape_tri_start });
        }
    }

    // Fill any corner left at a zero normal - kataglyphis.vulkan.vertex's
    // fillMissingFlatNormals. Covers both the no-`vn`-at-all file (every
    // corner is zero, so this behaves exactly like computeFlatNormals) and
    // the mixed file where only some faces carry `vn` (only the zero corners
    // are touched). Vertex dedup (`:363-365` above) keys on the whole
    // Vertex including the normal, so a corner shared between faces with
    // different geometric normals resolves to whichever triangle is visited
    // last - the same flat approximation computeFlatNormals already makes,
    // not a new limitation.
    vertex::fillMissingFlatNormals(vertices, indices);
}
