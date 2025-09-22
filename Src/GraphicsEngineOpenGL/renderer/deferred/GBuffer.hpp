#pragma once
#include <glad/glad.h>
// #include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>

#include "hostDevice/GlobalValues.hpp"
#include "renderer/ShaderProgram.hpp"

class GBuffer
{
  public:
    GBuffer();
    GBuffer(GLint window_width, GLint window_height);

    void create();
    void read(std::shared_ptr<ShaderProgram> shader_program);

    void update_window_params(GLuint window_width, GLuint window_height);

    GLuint get_id() const { return g_buffer; };

    ~GBuffer();

  private:
    GLuint g_buffer;

    GLuint g_position, g_normal, g_albedo, g_material_id, g_depth;

    GLuint g_buffer_attachment[G_BUFFER_SIZE] = { GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3 };

    GLuint window_width, window_height;
};
