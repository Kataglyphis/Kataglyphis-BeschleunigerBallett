#include "ObjLoader.hpp"
#include <string>
#include <vector>
#include "scene/Vertex.hpp"
#include "scene/ObjMaterial.hpp"
#include <glm/ext/vector_float4.hpp>
#include <cstdlib>
#include <glad/glad.h>
#include <stdexcept>
#include <sstream>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <glm/ext/vector_float2.hpp>
#include <glm/geometric.hpp>

#define TINYOBJLOADER_IMPLEMENTATION
#include "hostDevice/host_device_shared.hpp"
#include <filesystem>
#include <iostream>
#include <tiny_obj_loader.h>
#include <unordered_map>

ObjLoader::ObjLoader() = default;

void ObjLoader::load(const std::string &modelFile,
  std::vector<Vertex> &vertices,
  std::vector<unsigned int> &indices,
  std::vector<std::string> &texture_list,
  std::vector<ObjMaterial> &materials,
  std::vector<glm::vec4> &materialIndex)
{
    tinyobj::ObjReaderConfig const reader_config;
    tinyobj::ObjReader reader;

    if (!reader.ParseFromFile(modelFile, reader_config)) {
        if (!reader.Error().empty()) { std::cerr << "TinyObjReader: " << reader.Error(); }
        exit(EXIT_FAILURE);
    }

    if (!reader.Warning().empty()) { std::cout << "TinyObjReader: " << reader.Warning(); }

    const auto &tol_materials = reader.GetMaterials();
    // texture_list.reserve(tol_materials.size());

    if (tol_materials.size() > MAX_MATERIALS) {
        std::cerr << "ObjLoader: We try to load more materials then MAX_MATERIALS is defined!\n";
        exit(EXIT_FAILURE);
    }

    // texture at position 0 is plain texture to handle non existing materials
    int texture_id = 1;

    std::stringstream texture_base_dir;
    std::filesystem::path const cwd = std::filesystem::current_path();
    texture_base_dir << cwd.string();
    texture_base_dir << RELATIVE_RESOURCE_PATH << "Textures/plain.png";
    texture_list.push_back(texture_base_dir.str());

    // we now iterate over all materials to get diffuse textures
    for (const auto &tol_material : tol_materials) {
        const tinyobj::material_t *mp = &tol_material;
        ObjMaterial material;
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
            std::string const texture_filename = get_base_dir(modelFile) + "/" + relative_texture_filename;

            texture_list.push_back(texture_filename);

            material.textureID = texture_id;
            texture_id++;

        } else {
            // this means no texture was assigned; we catch it here and assign our
            // plain texture at position 0
            material.textureID = 0;
        }

        materials.push_back(material);
    }

    // for the case no .mtl file is given place some random standard material ...
    if (tol_materials.empty()) { materials.emplace_back(); }

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
                tinyobj::real_t const vx = attrib.vertices[(3 * static_cast<size_t>(idx.vertex_index)) + 0];
                tinyobj::real_t const vy = attrib.vertices[(3 * static_cast<size_t>(idx.vertex_index)) + 1];
                tinyobj::real_t const vz = attrib.vertices[(3 * static_cast<size_t>(idx.vertex_index)) + 2];
                glm::vec3 const pos = { vx, vy, vz };

                minX = std::min(minX, pos.x);
                maxX = std::max(maxX, pos.x);
                minY = std::min(minY, pos.y);
                maxY = std::max(maxY, pos.y);
                minZ = std::min(minZ, pos.z);
                maxZ = std::max(maxZ, pos.z);

                glm::vec3 normals(0.0F);
                // Check if `normal_index` is zero or positive. negative = no normal
                // data
                if (idx.normal_index >= 0 && !attrib.normals.empty()) {
                    tinyobj::real_t const nx = attrib.normals[(3 * static_cast<size_t>(idx.normal_index)) + 0];
                    tinyobj::real_t const ny = attrib.normals[(3 * static_cast<size_t>(idx.normal_index)) + 1];
                    tinyobj::real_t const nz = attrib.normals[(3 * static_cast<size_t>(idx.normal_index)) + 2];
                    normals = glm::vec3(nx, ny, nz);
                }

                glm::vec3 color(-1.F);
                if (!attrib.colors.empty()) {
                    tinyobj::real_t const red = attrib.colors[(3 * static_cast<size_t>(idx.vertex_index)) + 0];
                    tinyobj::real_t const green = attrib.colors[(3 * static_cast<size_t>(idx.vertex_index)) + 1];
                    tinyobj::real_t const blue = attrib.colors[(3 * static_cast<size_t>(idx.vertex_index)) + 2];
                    color = glm::vec3(red, green, blue);
                }

                glm::vec2 tex_coords(0.0F);
                // Check if `texcoord_index` is zero or positive. negative = no texcoord
                // data
                if (idx.texcoord_index >= 0 && !attrib.texcoords.empty()) {
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
            materialIndex.emplace_back(shape.mesh.material_ids[f], 0.0F, 0.0F, 0.0F);
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

ObjLoader::~ObjLoader() = default;
