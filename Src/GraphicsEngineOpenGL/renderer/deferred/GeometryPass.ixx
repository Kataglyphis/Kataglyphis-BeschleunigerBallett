module;

#include <array>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <glad/glad.h>

export module kataglyphis.opengl.geometry_pass;

import kataglyphis.opengl.camera;
import kataglyphis.opengl.geometry_pass_shader_program;
import kataglyphis.opengl.render_pass_scene_dependend;
import kataglyphis.opengl.scene;
import kataglyphis.opengl.sky_box;
import kataglyphis.opengl.texture;

export class GeometryPass final : public RenderPassSceneDependend
{
  public:
    GeometryPass();

    void execute(glm::mat4 projection_matrix,
      const std::shared_ptr<Camera> &main_camera,
      GLuint window_width,
      GLuint window_height,
      GLuint gbuffer_id,
      GLfloat delta_time,
      const std::shared_ptr<Scene> &);

    void create_shader_program();

    void set_game_object_uniforms(glm::mat4 model, glm::mat4 normal_model);

    ~GeometryPass();

  private:
    std::shared_ptr<GeometryPassShaderProgram> shader_program;

    SkyBox skybox;
};
