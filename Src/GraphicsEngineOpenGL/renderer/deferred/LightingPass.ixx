module;

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>

export module kataglyphis.opengl.lighting_pass;

import kataglyphis.opengl.camera;
import kataglyphis.opengl.gbuffer;
import kataglyphis.opengl.lighting_pass_shader_program;
import kataglyphis.opengl.render_pass;
import kataglyphis.opengl.scene;
import kataglyphis.opengl.texture;
import kataglyphis.opengl.quad;

export class LightingPass final : public RenderPass
{
  public:
    LightingPass();

    void execute(glm::mat4 projection_matrix,
      const std::shared_ptr<Camera> &main_camera,
      const std::shared_ptr<Scene> &scene,
      const std::shared_ptr<GBuffer> &gbuffer,
      float delta_time);

    void create_shader_program() override;

    ~LightingPass();

  private:
    glm::vec3 current_offset;

    void set_uniforms(glm::mat4 projection_matrix,
      const std::shared_ptr<Camera> &main_camera,
      const std::shared_ptr<Scene> &scene,
      const std::shared_ptr<GBuffer> &gbuffer,
      float delta_time);

    std::shared_ptr<LightingPassShaderProgram> shader_program;

    Quad quad;
};
