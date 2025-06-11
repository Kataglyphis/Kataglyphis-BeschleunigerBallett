#include "LoadingScreen.hpp"
#include "renderer/OpenGLRendererConfig.hpp"

#include "scene/texture/RepeatMode.hpp"
#include <filesystem>
#include <sstream>

LoadingScreen::LoadingScreen() { create_shader_program(); }

void LoadingScreen::init()
{
    std::stringstream texture_base_dir;
    std::filesystem::path cwd = std::filesystem::current_path();
    texture_base_dir << cwd.string();
    texture_base_dir << RELATIVE_RESOURCE_PATH;
    texture_base_dir << "Textures/";

    std::stringstream texture_loading_screen;
    texture_loading_screen << texture_base_dir.str() << "Loading_Screen/ukraine.jpg";

    loading_screen_tex = Texture(texture_loading_screen.str().c_str(), std::make_shared<RepeatMode>());
    loading_screen_tex.load_texture_without_alpha_channel();

    std::stringstream texture_logo;
    texture_logo << texture_base_dir.str() << "Loading_Screen/Engine_logo.png";
    logo_tex = Texture(texture_logo.str().c_str(), std::make_shared<RepeatMode>());
    logo_tex.load_texture_with_alpha_channel();
}

void LoadingScreen::render()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    loading_screen_tex.use_texture(0);

    loading_screen_shader_program->use_shader_program();

    loading_screen_quad.render();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void LoadingScreen::create_shader_program()
{
    loading_screen_shader_program = std::make_shared<ShaderProgram>(ShaderProgram{});

    loading_screen_shader_program->create_from_files(
      "loading_screen/loading_screen.vert", "loading_screen/loading_screen.frag");
}

LoadingScreen::~LoadingScreen() {}
