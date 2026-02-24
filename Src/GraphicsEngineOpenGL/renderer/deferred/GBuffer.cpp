module;

#include <cstdio>
#include <glad/glad.h>
#include <memory>
#include <print>

#include "hostDevice/GlobalValues.hpp"
#include "hostDevice/bindings.hpp"

module kataglyphis.opengl.gbuffer;

import kataglyphis.opengl.shader_program;

GBuffer::GBuffer() : window_width(1024), window_height(768) {}

GBuffer::GBuffer(GLint window_width, GLint window_height) : window_width(window_width), window_height(window_height) {}

void GBuffer::create()
{
    glGenFramebuffers(1, &g_buffer);
    glBindFramebuffer(GL_FRAMEBUFFER, g_buffer);

    glGenTextures(1, &g_position);
    glBindTexture(GL_TEXTURE_2D, g_position);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, window_width, window_height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, g_buffer_attachment[0], GL_TEXTURE_2D, g_position, 0);

    glGenTextures(1, &g_normal);
    glBindTexture(GL_TEXTURE_2D, g_normal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, window_width, window_height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, g_buffer_attachment[1], GL_TEXTURE_2D, g_normal, 0);

    glGenTextures(1, &g_albedo);
    glBindTexture(GL_TEXTURE_2D, g_albedo);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, window_width, window_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, g_buffer_attachment[2], GL_TEXTURE_2D, g_albedo, 0);

    glGenTextures(1, &g_material_id);
    glBindTexture(GL_TEXTURE_2D, g_material_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, window_width, window_height, 0, GL_RGBA, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, g_buffer_attachment[3], GL_TEXTURE_2D, g_material_id, 0);

    glDrawBuffers(G_BUFFER_SIZE, g_buffer_attachment);

    // create and attach depth buffer (renderbuffer)
    // renderbuffers are a bit more performant
    glGenRenderbuffers(1, &g_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, g_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, window_width, window_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_depth);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::print("Framebuffer not complete!");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GBuffer::read(const std::shared_ptr<ShaderProgram> &shader_program) const
{
    // GBUFFER
    GLuint texture_index = GBUFFER_TEXTURES_SLOT;
    shader_program->setUniformInt(texture_index, "g_position");
    glActiveTexture(GL_TEXTURE0 + texture_index);
    glBindTexture(GL_TEXTURE_2D, g_position);

    texture_index++;
    shader_program->setUniformInt(texture_index, "g_normal");
    glActiveTexture(GL_TEXTURE0 + texture_index);
    glBindTexture(GL_TEXTURE_2D, g_normal);

    texture_index++;
    shader_program->setUniformInt(texture_index, "g_albedo");
    glActiveTexture(GL_TEXTURE0 + texture_index);
    glBindTexture(GL_TEXTURE_2D, g_albedo);

    texture_index++;
    shader_program->setUniformInt(texture_index, "g_material_id");
    glActiveTexture(GL_TEXTURE0 + texture_index);
    glBindTexture(GL_TEXTURE_2D, g_material_id);
}

void GBuffer::update_window_params(GLuint window_width, GLuint window_height)
{
    this->window_width = window_width;
    this->window_height = window_height;
}

GBuffer::~GBuffer()
{
    glDeleteFramebuffers(1, &g_buffer);
    glDeleteTextures(1, &g_position);
    glDeleteTextures(1, &g_normal);
    glDeleteTextures(1, &g_albedo);
    glDeleteTextures(1, &g_material_id);
}
