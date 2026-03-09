module;

#include <memory>
#include <glad/glad.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <cstdint>
#include <sstream>
#include <vector>

export module kataglyphis.opengl.omni_shadow_map_pass;

import kataglyphis.opengl.render_pass_scene_dependend;
import kataglyphis.opengl.point_light;
import kataglyphis.opengl.point_light.omni_dir_shadow_map;
import kataglyphis.opengl.scene;
import kataglyphis.opengl.omni_dir_shadow_shader_program;

export class OmniShadowMapPass final : public RenderPassSceneDependend
{
  public:
    OmniShadowMapPass();

    void execute(const std::shared_ptr<PointLight> &p_light, const std::shared_ptr<Scene> &scene);

    void set_game_object_uniforms(glm::mat4 model, glm::mat4 normal_model) override;

    void create_shader_program() override;

    ~OmniShadowMapPass();

  private:
    std::shared_ptr<OmniDirShadowShaderProgram> shader_program;
};
