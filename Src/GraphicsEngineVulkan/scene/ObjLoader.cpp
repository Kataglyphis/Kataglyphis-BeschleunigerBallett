module;

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>
#include "spdlog/spdlog.h"
#define TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_DISABLE_FAST_FLOAT
#include <glm/ext/vector_float2.hpp>
#include <glm/geometric.hpp>
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

using namespace Kataglyphis;

ObjLoader::ObjLoader(std::shared_ptr<VulkanDevice>device, vk::Queue transfer_queue, vk::CommandPool command_pool)
  : device(device), transfer_queue(transfer_queue), command_pool(command_pool)
{}

auto ObjLoader::loadModel(const std::string &modelFile) -> std::shared_ptr<Model>
{
    // clear prior state if loadModel is called multiple times on the same instance
    textures.clear();
    materials.clear();
    vertices.clear();
    indices.clear();
    materialIndex.clear();

    // ONE parse, shared by both extraction passes below. This used to happen
    // inside each of them, so every model was read and tokenised twice.
    tinyobj::ObjReaderConfig const reader_config;
    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(modelFile, reader_config)) {
        // Must not kill the process: the GUI model picker can hand this
        // arbitrary files. loadVertices already returned gracefully here, but
        // loadTexturesAndMaterials called exit(EXIT_FAILURE) and ran first,
        // so a malformed asset took the application down regardless.
        if (!reader.Error().empty()) { spdlog::error("TinyObjReader: {}", reader.Error()); }
        return nullptr;
    }
    if (!reader.Warning().empty()) { spdlog::warn("TinyObjReader: {}", reader.Warning()); }

    // the model we want to load
    std::shared_ptr<Model> new_model = std::make_shared<Model>(device);

    // first load txtures from model
    std::vector<std::string> textureNames = loadTexturesAndMaterials(reader, modelFile);

    // now that we have the names lets create the vulkan side of textures
    for (size_t i = 0; i < textureNames.size(); i++) {
        // If material had no texture, set '0' to indicate no texture, texture 0
        // will be reserved for a default texture
        if (!textureNames[i].empty()) {
            Texture texture;
            if (texture.createFromFile(device, command_pool, textureNames[i])) {
                new_model->addTexture(std::move(texture));
            }
        }
    }

    if (new_model->getTextureCount() == 0) {
        Texture defaultTexture;
        defaultTexture.createDefaultTexture(device, command_pool);
        new_model->addTexture(std::move(defaultTexture));
    }

    loadVertices(reader);

    new_model->add_new_mesh(device, transfer_queue, command_pool, vertices, indices, materialIndex, this->materials);

    return new_model;
}

auto ObjLoader::loadTexturesAndMaterials(const tinyobj::ObjReader &reader, const std::string &modelFile)
  -> std::vector<std::string>
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
            std::string const texture_filename = model_file.getBaseDir() + "/textures/" + relative_texture_filename;

            textures.push_back(texture_filename);
            material.textureID = texture_id;
            texture_id++;

        } else {
            material.textureID = 0;
            textures.emplace_back("");
        }

        materials.push_back(material);
    }

    // for the case no .mtl file is given place some random standard material ...
    if (tol_materials.empty()) { materials.emplace_back(); }

    return textures;
}

void ObjLoader::loadVertices(const tinyobj::ObjReader &reader)
{
    const auto &attrib = reader.GetAttrib();
    const auto &shapes = reader.GetShapes();
    std::unordered_map<Vertex, uint32_t> vertices_map{};

    // Loop over shapes
    for (const auto &shape : shapes) {
        // prepare for enlargement
        vertices.reserve(shape.mesh.indices.size() + vertices.size());
        indices.reserve(shape.mesh.indices.size() + indices.size());

        // Loop over faces(polygon)
        size_t index_offset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            auto const fv = static_cast<size_t>(shape.mesh.num_face_vertices[f]);

            // Loop over vertices in the face.
            for (size_t v = 0; v < fv; v++) {
                // access to vertex
                tinyobj::index_t const idx = shape.mesh.indices[index_offset + v];
                // Malformed files can carry negative or out-of-range indices
                // (fuzz-found hazard): validate before every array access.
                const auto vertex_index = static_cast<size_t>(idx.vertex_index);
                if (idx.vertex_index < 0 || (3 * vertex_index) + 2 >= attrib.vertices.size()) {
                    continue;
                }
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

                glm::vec3 color(-1.F);
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
                    tinyobj::real_t const tx = attrib.texcoords[(2 * static_cast<size_t>(idx.texcoord_index)) + 0];
                    // flip y coordinate !!
                    tinyobj::real_t const ty =
                      1.F - attrib.texcoords[(2 * static_cast<size_t>(idx.texcoord_index)) + 1];
                    tex_coords = glm::vec2(tx, ty);
                }

                Vertex const vert{ pos, normals, color, tex_coords };

                if (!vertices_map.contains(vert)) {
                    vertices_map[vert] = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vert);
                }

                indices.push_back(vertices_map[vert]);
            }

            index_offset += fv;

            // per-face material; face usually is triangle
            // matToTex[shapes[s].mesh.material_ids[f]]
            materialIndex.push_back(static_cast<uint32_t>(shape.mesh.material_ids[f]));
        }
    }

    // precompute normals if no provided
    if (attrib.normals.empty()) {
        for (size_t i = 0; i < indices.size(); i += 3) {
            Vertex &v0 = vertices[indices[i + 0]];
            Vertex &v1 = vertices[indices[i + 1]];
            Vertex &v2 = vertices[indices[i + 2]];

            glm::vec3 const n = glm::normalize(glm::cross((v1.position - v0.position), (v2.position - v0.position)));
            v0.normal = n;
            v1.normal = n;
            v2.normal = n;
        }
    }
}
