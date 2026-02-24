module;

#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <glad/glad.h>

export module kataglyphis.opengl.directional_shadow_map_pass;

import kataglyphis.opengl.camera;
import kataglyphis.opengl.render_pass_scene_dependend;
import kataglyphis.opengl.directional_light;
import kataglyphis.opengl.directional_light.cascaded_shadow_map;
import kataglyphis.opengl.scene;
import kataglyphis.opengl.shader_program;
import kataglyphis.opengl.view_frustum_culling;

export class DirectionalShadowMapPass final : public RenderPassSceneDependend
{
  public:
    DirectionalShadowMapPass();

    void execute(glm::mat4 projection,
      const std::shared_ptr<Camera> &main_camera,
      GLuint window_width,
      GLuint window_height,
      const std::shared_ptr<Scene> &scene);

    void create_shader_program();

    void set_game_object_uniforms(glm::mat4 model, glm::mat4 normal_model);

    ~DirectionalShadowMapPass();

  private:
    std::shared_ptr<ShaderProgram> shader_program;
};
