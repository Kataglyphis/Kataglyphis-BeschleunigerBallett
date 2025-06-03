#pragma once
#include <vector>

// #include "Model.hpp"
#include "renderer/deferred/RenderPass.hpp"
#include <glm/glm.hpp>

class RenderPassSceneDependend : public RenderPass
{
  public:
    RenderPassSceneDependend();

    virtual void set_game_object_uniforms(glm::mat4 model, glm::mat4 normal_model) = 0;
    virtual void create_shader_program() = 0;

    ~RenderPassSceneDependend();

  private:
};
