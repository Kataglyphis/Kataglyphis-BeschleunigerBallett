module;

#include <glm/mat4x4.hpp>

export module kataglyphis.opengl.render_pass_scene_dependend;

import kataglyphis.opengl.render_pass;

export class RenderPassSceneDependend : public RenderPass
{
  public:
    RenderPassSceneDependend();

    virtual void set_game_object_uniforms(glm::mat4 model, glm::mat4 normal_model) = 0;
    virtual void create_shader_program() = 0;

    ~RenderPassSceneDependend();

  private:
};
