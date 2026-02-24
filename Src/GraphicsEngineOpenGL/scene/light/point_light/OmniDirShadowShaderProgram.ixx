module;

export module kataglyphis.opengl.omni_dir_shadow_shader_program;

import kataglyphis.opengl.shader_program;

export class OmniDirShadowShaderProgram : public ShaderProgram
{
  public:
    OmniDirShadowShaderProgram();

    void reload();

    ~OmniDirShadowShaderProgram();

  private:
};
