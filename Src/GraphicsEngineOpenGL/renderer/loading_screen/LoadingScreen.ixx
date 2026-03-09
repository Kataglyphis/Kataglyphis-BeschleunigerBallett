module;

#include <memory>

export module kataglyphis.opengl.loading_screen;

import kataglyphis.opengl.shader_program;
import kataglyphis.opengl.quad;
import kataglyphis.opengl.texture;

export class LoadingScreen
{
  public:
    LoadingScreen();

    void init();
    void render();

    ~LoadingScreen();

  private:
    Quad loading_screen_quad;
    std::unique_ptr<Texture> loading_screen_tex;
    std::unique_ptr<Texture> logo_tex;

    std::shared_ptr<ShaderProgram> loading_screen_shader_program;

    void create_shader_program();
};
