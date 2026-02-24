module;

#include <glad/glad.h>

export module kataglyphis.opengl.lighting_pass_shader_program;

import kataglyphis.opengl.shader_program;

export class LightingPassShaderProgram : public ShaderProgram
{
  public:
    LightingPassShaderProgram();

    ~LightingPassShaderProgram();
};
