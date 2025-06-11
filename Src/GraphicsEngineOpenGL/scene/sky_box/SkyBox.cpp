#include "scene/sky_box/SkyBox.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <random>
#include <sstream>
#include <time.h>
#include <vector>

#include <stb_image.h>

#include "hostDevice/bindings.hpp"
#include "renderer/OpenGLRendererConfig.hpp"
#include "scene/Vertex.hpp"

SkyBox::SkyBox()
{
    std::stringstream skybox_base_dir;
    std::filesystem::path cwd = std::filesystem::current_path();
    skybox_base_dir << cwd.string();
    skybox_base_dir << RELATIVE_RESOURCE_PATH;
    skybox_base_dir << "Textures/Skybox/DOOM2016/";

    std::stringstream texture_loading;
    std::array<std::string, 6> skybox_textures = {
        "DOOM16RT.png", "DOOM16LF.png", "DOOM16UP.png", "DOOM16DN.png", "DOOM16FT.png", "DOOM16BK.png"
    };

    std::vector<std::string> skybox_faces;

    for (uint32_t i = 0; i < static_cast<uint32_t>(skybox_textures.size()); i++) {
        texture_loading << skybox_base_dir.str() << skybox_textures[i];
        skybox_faces.push_back(texture_loading.str());
        texture_loading.str(std::string());
    }

    // time_t timer;
    // srand(time(&timer));
    srand(0);
    shader_playback_time = 1;

    shader_program = std::make_shared<ShaderProgram>();
    shader_program->create_from_files("skybox/SkyBox.vert", "skybox/SkyBox.frag");

    // texture setup
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture_id);

    int width, height, bit_depth;

    for (size_t i = 0; i < 6; i++) {
        unsigned char *texture_data = stbi_load(skybox_faces[i].c_str(), &width, &height, &bit_depth, 0);
        if (!texture_data) {
            printf("Failed to find: %s\n", skybox_faces[i].c_str());
            return;
        }

        glTexImage2D(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i),
          0,
          GL_RGBA,
          width,
          height,
          0,
          GL_RGBA,
          GL_UNSIGNED_BYTE,
          texture_data);

        stbi_image_free(texture_data);
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Mesh Setup
    std::vector<unsigned int> sky_box_indices = { // front
        0,
        1,
        2,
        2,
        1,
        3,
        // right
        2,
        3,
        5,
        5,
        3,
        7,
        // back
        5,
        7,
        4,
        4,
        7,
        6,
        // left
        4,
        6,
        0,
        0,
        6,
        1,
        // top
        4,
        0,
        5,
        5,
        0,
        2,
        // bottom
        1,
        6,
        3,
        3,
        6,
        7
    };

    std::vector<Vertex> sky_box_vertices = {

        Vertex(glm::vec3(-1.0f, 1.0f, -1.0f),
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec2(0.0f, 0.0f)),

        Vertex(glm::vec3(-1.0f, -1.0f, -1.0f),
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec2(0.0f, 0.0f)),

        Vertex(glm::vec3(1.0f, 1.0f, -1.0f),
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec2(0.0f, 0.0f)),

        Vertex(glm::vec3(1.0f, -1.0f, -1.0f),
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec2(0.0f, 0.0f)),

        Vertex(glm::vec3(-1.0f, 1.0f, 1.0f),
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec2(0.0f, 0.0f)),

        Vertex(
          glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(0.0f, 0.0f)),

        Vertex(glm::vec3(-1.0f, -1.0f, 1.0f),
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec2(0.0f, 0.0f)),

        Vertex(glm::vec3(1.0f, -1.0f, 1.0f),
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec2(0.0f, 0.0f)),

    };

    sky_mesh = std::make_shared<Mesh>(sky_box_vertices, sky_box_indices);
}

void SkyBox::draw_sky_box(glm::mat4 projection_matrix,
  glm::mat4 view_matrix,
  GLuint window_width,
  GLuint window_height,
  GLfloat delta_time)
{
    // https://learnopengl.com/Advanced-OpenGL/Cubemaps
    GLfloat velocity = movement_speed * delta_time;
    shader_playback_time = static_cast<GLfloat>(fmod(shader_playback_time + velocity, 10000));

    glm::mat4 new_view_matrix = glm::mat4(glm::mat3(view_matrix));

    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    // std::time_t now =
    // std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    shader_program->use_shader_program();

    shader_program->setUniformMatrix4fv(projection_matrix, "projection");
    shader_program->setUniformMatrix4fv(new_view_matrix, "view");
    shader_program->setUniformInt(SKYBOX_TEXTURES_SLOT, "skybox");

    glActiveTexture(GL_TEXTURE0 + SKYBOX_TEXTURES_SLOT);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture_id);

    shader_program->validate_program();

    sky_mesh->render();

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

void SkyBox::reload()
{
    shader_program = std::make_shared<ShaderProgram>();
    shader_program->create_from_files("Shaders/SkyBox.vert", "Shaders/SkyBox.frag");
}

SkyBox::~SkyBox() { glDeleteTextures(1, &texture_id); }
