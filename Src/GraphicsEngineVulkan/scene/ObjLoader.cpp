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

using namespace Kataglyphis;

ObjLoader::ObjLoader(std::shared_ptr<VulkanDevice>device, vk::CommandPool command_pool)
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
            if (texture.createFromFile(device, command_pool, textureNames[i])) {
                new_model->addTexture(std::move(texture));
            } else {
                // Load failed (e.g. a missing .mtl texture file). Occupy this
                // slot with the default texture instead of skipping it:
                // textureID is a dense counter over non-empty names, so a
                // skipped slot would shift every later texture down one and
                // make the final textureID index past the descriptor array.
                Texture defaultTexture;
                defaultTexture.createDefaultTexture(device, command_pool);
                new_model->addTexture(std::move(defaultTexture));
            }
        }
    }

    if (new_model->getTextureCount() == 0) {
        Texture defaultTexture;
        defaultTexture.createDefaultTexture(device, command_pool);
        new_model->addTexture(std::move(defaultTexture));
    }

    // One Mesh per OBJ shape (see MeshRange). Each mesh shares the full materials
    // array - its materialIndex still holds the original indices - matching the
    // glTF split; a per-mesh material subset is a later optimisation. A
    // single-shape OBJ (or the empty-range fallback) builds exactly one mesh, so
    // existing single-object models are behaviour-identical.
    if (meshRanges.empty()) {
        new_model->add_new_mesh(
          device, command_pool, vertices, indices, materialIndex, this->materials);
    } else {
        for (const MeshRange &range : meshRanges) {
            // Non-const: Model::add_new_mesh takes the arrays by non-const ref.
            MeshSlice slice = sliceMeshRange(range, vertices, indices, materialIndex);
            new_model->add_new_mesh(
              device, command_pool, slice.vertices, slice.indices, slice.materialIndex, this->materials);
        }
    }
    return new_model;
}

void ObjLoader::loadTexturesAndMaterials(const tinyobj::ObjReader &reader, const std::string &modelFile)
{
    const auto &tol_materials = reader.GetMaterials();
    textures.reserve(tol_materials.size());

    int texture_id = 0;

    // we now iterate over all materials to get diffuse textures
    for (const auto &tol_material : tol_materials) {
        const tinyobj::material_t *mp = &tol_material;
        ObjMaterial material{};
        material.ambient = glm::vec3(mp->ambient[0], mp->ambient[1], mp->ambient[2]);
        material.diffuse = glm::vec3(mp->diffuse[0], mp->diffuse[1], mp->diffuse[2]);
        material.specular = glm::vec3(mp->specular[0], mp->specular[1], mp->specular[2]);
        material.emission = glm::vec3(mp->emission[0], mp->emission[1], mp->emission[2]);
        material.transmittance = glm::vec3(mp->transmittance[0], mp->transmittance[1], mp->transmittance[2]);
        material.dissolve = mp->dissolve;
        material.ior = mp->ior;
        material.shininess = mp->shininess;
        material.illum = mp->illum;

        if (!mp->diffuse_texname.empty()) {
            std::string const relative_texture_filename = mp->diffuse_texname;
            File model_file(modelFile);
            std::string const base_dir = model_file.getBaseDir();

            // docs/model-loading.md: resolve relative to the directory
            // containing the .mtl first - what the OBJ/MTL format actually
            // specifies - and retry under a textures/ subdirectory of that
            // same directory, because every shipped asset in this repo puts
            // its textures there instead of beside the .mtl.
            std::string const beside_mtl = base_dir + "/" + relative_texture_filename;
            std::string const under_textures = base_dir + "/textures/" + relative_texture_filename;
            std::string texture_filename;
            std::error_code beside_mtl_ec;
            std::error_code under_textures_ec;
            if (std::filesystem::exists(beside_mtl, beside_mtl_ec) && !beside_mtl_ec) {
                texture_filename = beside_mtl;
            } else if (std::filesystem::exists(under_textures, under_textures_ec) && !under_textures_ec) {
                texture_filename = under_textures;
            } else {
                // Neither candidate exists. Keep today's behaviour - record a
                // path and let uploadParsed substitute the default texture -
                // but warn loudly: a wrong path degrading silently to white
                // is the failure mode this whole resolution rule is about.
                texture_filename = beside_mtl;
                spdlog::warn(
                  "map_Kd '{}' not found beside the .mtl ('{}') or under textures/ ('{}'); the model will "
                  "render with the default texture",
                  relative_texture_filename,
                  beside_mtl,
                  under_textures);
            }

            textures.push_back(texture_filename);
            material.textureID = texture_id;
            texture_id++;

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
                    // sentinel that would have darkened/inverted the surface.
                    glm::vec3 color(1.F);
                    if ((3 * vertex_index) + 2 < attrib.colors.size()) {
                        tinyobj::real_t const red = attrib.colors[(3 * vertex_index) + 0];
                        tinyobj::real_t const green = attrib.colors[(3 * vertex_index) + 1];
                        tinyobj::real_t const blue = attrib.colors[(3 * vertex_index) + 2];
                        color = glm::vec3(red, green, blue);
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

    // precompute normals if no provided - kataglyphis.vulkan.vertex's
    // computeFlatNormals, the shared copy of this loop (also used by GltfLoader).
    if (attrib.normals.empty()) { vertex::computeFlatNormals(vertices, indices); }
}
