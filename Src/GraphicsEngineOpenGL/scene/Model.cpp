#include "Model.hpp"

#include "hostDevice/bindings.hpp"

#include "scene/AABB.hpp"
#include "scene/ObjMaterial.hpp"
#include "scene/Mesh.hpp"
#include "scene/texture/RepeatMode.hpp"
#include "scene/texture/Texture.hpp"

#include <cstdint>
#include <cstdio>
#include <glad/glad.h>
#include <glm/ext/vector_float4.hpp>
#include <memory>
#include <string>
#include <print>
#include <vector>
#include <utility>

Model::Model() : aabb(std::make_shared<AABB>()) {}

auto Model::get_aabb() -> std::shared_ptr<AABB> { return aabb; }

auto Model::get_materials() const -> std::vector<ObjMaterial> { return materials; }

auto Model::get_texture_count() const -> int { return static_cast<uint32_t>(texture_list.size()); }

void Model::load_model_in_ram(const std::string &model_path)
{
    loader = ObjLoader();
    loader.load(model_path, vertices, indices, textures, materials, materialIndex);
}

// all OpenGL calls need to be on the same thread!
// hence we have to decouple the loading task from all OpenGL agnostic code
void Model::create_render_context()
{
    texture_list.resize(textures.size());

    for (uint32_t i = 0; i < static_cast<uint32_t>(textures.size()); i++) {
        texture_list[i] = std::make_shared<Texture>(textures[i].c_str(), std::make_shared<RepeatMode>());

        if (!texture_list[i]->load_SRGB_texture_without_alpha_channel()) {
            std::println("Failed to load texture at: {}", textures[i]);
            texture_list[i].reset();
        }
    }

    mesh = std::make_shared<Mesh>(vertices, indices);

    // https://www.khronos.org/opengl/wiki/Shader_Storage_Buffer_Object
    glGenBuffers(1, &ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
      materialIndex.size() * sizeof(glm::vec4),
      materialIndex.data(),
      GL_STREAM_READ);// sizeof(data) only works for statically sized
                      // C/C++ arrays.
}

void Model::bind_ressources()
{
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, STORAGE_BUFFER_MATERIAL_ID_BINDING, ssbo);
    for (int i = 0; std::cmp_less(i, texture_list.size()); i++) {
        texture_list[i]->use_texture(i + MODEL_TEXTURES_SLOT);
    }
}

void Model::unbind_resources()
{
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    for (int i = 0; std::cmp_less(i, texture_list.size()); i++) {
        texture_list[i]->unbind_texture(i + MODEL_TEXTURES_SLOT);
    }
}

void Model::render() { mesh->render(); }

Model::~Model()
{
    // unbind material index buffer
}
