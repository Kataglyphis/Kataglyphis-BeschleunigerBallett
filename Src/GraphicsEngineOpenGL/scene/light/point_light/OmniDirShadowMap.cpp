#include "OmniDirShadowMap.hpp"
#include "scene/shadows/ShadowMap.hpp"
#include <glad/glad.h>
#include <cstdio>

#include <print>

OmniDirShadowMap::OmniDirShadowMap() = default;

auto OmniDirShadowMap::init(GLuint width, GLuint height) -> bool
{
    shadow_width = width;
    shadow_height = height;

    glGenFramebuffers(1, &FBO);

    glGenTextures(1, &shadow_map);
    glBindTexture(GL_TEXTURE_CUBE_MAP, shadow_map);

    for (size_t i = 0; i < 6; i++) {
        // keep in mind that all following f.e. negative_x, positive_y,...etc. are
        // reachable by simply increment positive_x stepwise
        glTexImage2D(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i),
          0,
          GL_DEPTH_COMPONENT,
          shadow_width,
          shadow_height,
          0,
          GL_DEPTH_COMPONENT,
          GL_FLOAT,
          nullptr);
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, FBO);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadow_map, 0);

    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    GLenum const status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::println("Framebuffer error: {}", status);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return true;
}

void OmniDirShadowMap::write() { glBindFramebuffer(GL_FRAMEBUFFER, FBO); }

void OmniDirShadowMap::read(GLenum texture_unit)
{
    glActiveTexture(GL_TEXTURE0 + texture_unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, shadow_map);
}

OmniDirShadowMap::~OmniDirShadowMap() = default;
