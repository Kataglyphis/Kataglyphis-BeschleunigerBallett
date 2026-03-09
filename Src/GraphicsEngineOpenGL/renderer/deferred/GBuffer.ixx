module;

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>

#include "hostDevice/GlobalValues.hpp"

export module kataglyphis.opengl.gbuffer;

import kataglyphis.opengl.shader_program;

export class GBuffer
{
  public:
    GBuffer();
    GBuffer(GLuint window_width, GLuint window_height);

    void create();
    void read(const std::shared_ptr<ShaderProgram> &shader_program) const;

    void update_window_params(GLuint in_window_width, GLuint in_window_height);

    GLuint get_id() const { return g_buffer; };

    ~GBuffer();

  private:
    GLuint g_buffer{};

    GLuint g_position{}, g_normal{}, g_albedo{}, g_material_id{}, g_depth{};

    GLuint g_buffer_attachment[G_BUFFER_SIZE] = { GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3 };

    GLuint window_width, window_height;
};
