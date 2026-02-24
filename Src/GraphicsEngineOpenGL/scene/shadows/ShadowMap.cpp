module;
#include <glad/glad.h>
#include <cstdio>
#include <print>

module kataglyphis.opengl.shadows.shadow_map;

ShadowMap::ShadowMap()
  :

    FBO(0), shadow_map(0), shadow_width(0), shadow_height(0)

{}

auto ShadowMap::init(GLuint width, GLuint height) -> bool
{
    shadow_width = width;
    shadow_height = height;

    glGenFramebuffers(1, &FBO);

    glGenTextures(1, &shadow_map);
    glBindTexture(GL_TEXTURE_2D, shadow_map);
    glTexImage2D(
      GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, shadow_width, shadow_height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    float border_color[] = { 1.0F, 1.0F, 1.0F, 1.0F };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border_color);

    glBindFramebuffer(GL_FRAMEBUFFER, FBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_map, 0);

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

void ShadowMap::write() { glBindFramebuffer(GL_FRAMEBUFFER, FBO); }

void ShadowMap::read(GLenum texture_unit)
{
    glActiveTexture(texture_unit);
    glBindTexture(GL_TEXTURE_2D, shadow_map);
}

ShadowMap::~ShadowMap()
{
    if (FBO != 0u) { glDeleteFramebuffers(1, &FBO); }

    if (shadow_map != 0u) { glDeleteTextures(1, &shadow_map); }
}
