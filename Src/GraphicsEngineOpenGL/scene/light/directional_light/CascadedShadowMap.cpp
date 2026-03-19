module;
#include <glad/glad.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <cstddef>
#include <vector>

#include "hostDevice/bindings.hpp"
#include "hostDevice/host_device_shared.hpp"
#include "spdlog/spdlog.h"

module kataglyphis.opengl.directional_light.cascaded_shadow_map;

CascadedShadowMap::CascadedShadowMap()
  :

    FBO(0), shadow_maps(0), shadow_width(0), shadow_height(0), matrices_UBO(0), num_active_cascades(0), pcf_radius(1),
    intensity(1)

{}

auto CascadedShadowMap::init(GLuint width, GLuint height, GLuint num_cascades) -> bool
{
    shadow_width = width;
    shadow_height = height;

    num_active_cascades = num_cascades;

    glGenFramebuffers(1, &FBO);
    glGenTextures(1, &shadow_maps);
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadow_maps);
    glTexImage3D(GL_TEXTURE_2D_ARRAY,
      0,
      GL_DEPTH_COMPONENT32F,
      shadow_width,
      shadow_height,
      NUM_CASCADES,
      0,
      GL_DEPTH_COMPONENT,
      GL_FLOAT,
      nullptr);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    constexpr float bordercolor[] = { 1.0F, 1.0F, 1.0F, 1.0F };
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, bordercolor);

    glBindFramebuffer(GL_FRAMEBUFFER, FBO);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadow_maps, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    GLenum const status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) { spdlog::error("ERROR::FRAMEBUFFER:: Framebuffer is not complete!"); }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // setting up our buffer for the light matrics
    // for every cascade we will have 1 matrix in the geometry shader
    glGenBuffers(1, &matrices_UBO);
    glBindBuffer(GL_UNIFORM_BUFFER, matrices_UBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(glm::mat4) * NUM_CASCADES, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, UNIFORM_LIGHT_MATRICES_BINDING, matrices_UBO);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    return true;
}

void CascadedShadowMap::write_light_matrices(std::vector<glm::mat4x4> &lightMatrices) const
{
    glBindBuffer(GL_UNIFORM_BUFFER, matrices_UBO);
    for (size_t i = 0; i < lightMatrices.size(); ++i) {
        glBufferSubData(GL_UNIFORM_BUFFER, i * sizeof(glm::mat4x4), sizeof(glm::mat4x4), &lightMatrices[i]);
    }
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void CascadedShadowMap::write() const { glBindFramebuffer(GL_FRAMEBUFFER, FBO); }

void CascadedShadowMap::read(GLenum texture_unit) const
{
    glActiveTexture(GL_TEXTURE0 + texture_unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadow_maps);
}

void CascadedShadowMap::set_pcf_radius(GLuint radius) { pcf_radius = radius; }

void CascadedShadowMap::set_intensity(GLfloat value) { this->intensity = value; }

CascadedShadowMap::~CascadedShadowMap()
{
    if (FBO != 0u) { glDeleteFramebuffers(1, &FBO); }

    if (shadow_maps != 0u) { glDeleteTextures(1, &shadow_maps); }
}
