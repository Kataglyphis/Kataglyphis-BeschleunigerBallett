#include "scene/light/directional_light/CascadedShadowMap.hpp"

#include <iostream>

#include "hostDevice/bindings.hpp"
#include "spdlog/spdlog.h"

CascadedShadowMap::CascadedShadowMap()
    :

      FBO(0),
      shadow_maps(0),
      shadow_width(0),
      shadow_height(0),
      matrices_UBO(0),
      num_active_cascades(0),
      pcf_radius(1),
      intensity(1)

{}

bool CascadedShadowMap::init(GLuint width, GLuint height, GLuint num_cascades) {
  shadow_width = width;
  shadow_height = height;

  num_active_cascades = num_cascades;

  glGenFramebuffers(1, &FBO);
  glGenTextures(1, &shadow_maps);
  glBindTexture(GL_TEXTURE_2D_ARRAY, shadow_maps);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, shadow_width,
               shadow_height, NUM_CASCADES, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
               nullptr);

  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

  constexpr float bordercolor[] = {1.0f, 1.0f, 1.0f, 1.0f};
  glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, bordercolor);

  glBindFramebuffer(GL_FRAMEBUFFER, FBO);
  glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadow_maps, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);

  int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
      spdlog::error("ERROR::FRAMEBUFFER:: Framebuffer is not complete!");
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // setting up our buffer for the light matrics
  // for every cascade we will have 1 matrix in the geometry shader
  glGenBuffers(1, &matrices_UBO);
  glBindBuffer(GL_UNIFORM_BUFFER, matrices_UBO);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(glm::mat4) * NUM_CASCADES, nullptr,
               GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_UNIFORM_BUFFER, UNIFORM_LIGHT_MATRICES_BINDING,
                   matrices_UBO);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);

  return true;
}

void CascadedShadowMap::write_light_matrices(
    std::vector<glm::mat4x4>& lightMatrices) {
  glBindBuffer(GL_UNIFORM_BUFFER, matrices_UBO);
  for (size_t i = 0; i < lightMatrices.size(); ++i) {
    glBufferSubData(GL_UNIFORM_BUFFER, i * sizeof(glm::mat4x4),
                    sizeof(glm::mat4x4), &lightMatrices[i]);
  }
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void CascadedShadowMap::write() { glBindFramebuffer(GL_FRAMEBUFFER, FBO); }

void CascadedShadowMap::read(GLenum texture_unit) {
  glActiveTexture(GL_TEXTURE0 + texture_unit);
  glBindTexture(GL_TEXTURE_2D_ARRAY, shadow_maps);
}

void CascadedShadowMap::set_pcf_radius(GLuint radius) { pcf_radius = radius; }

void CascadedShadowMap::set_intensity(GLfloat intensity) {
  this->intensity = intensity;
}

CascadedShadowMap::~CascadedShadowMap() {
  if (FBO) {
    glDeleteFramebuffers(1, &FBO);
  }

  if (shadow_maps) {
    glDeleteTextures(1, &shadow_maps);
  }
}
