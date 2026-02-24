module;

export module kataglyphis.opengl.compute_shader_program;

import kataglyphis.opengl.shader_program;

export class ComputeShaderProgram : public ShaderProgram
{
  public:
    ComputeShaderProgram();

    void reload();

    ~ComputeShaderProgram();

  private:
};
