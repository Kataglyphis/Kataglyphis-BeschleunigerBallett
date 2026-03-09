module;

#include <glad/glad.h>

export module kataglyphis.opengl.geometry_pass_shader_program;

import kataglyphis.opengl.shader_program;

export class GeometryPassShaderProgram : public ShaderProgram
{
  public:
    GeometryPassShaderProgram();

    GLuint get_program_id() { return program_id; }

    ~GeometryPassShaderProgram();

  protected:
};
