#pragma once

#include "OmniDirShadowShaderProgram.hpp"
#include "scene/light/point_light/PointLight.hpp"
#include "scene/Scene.hpp"
#include "renderer/RenderPassSceneDependend.hpp"

class OmniShadowMapPass : public RenderPassSceneDependend {
 public:
  OmniShadowMapPass();

  void execute(std::shared_ptr<PointLight> p_light,
               std::shared_ptr<Scene> scene);

  void set_game_object_uniforms(glm::mat4 model, glm::mat4 normal_model);

  void create_shader_program();

  ~OmniShadowMapPass();

 private:
  std::shared_ptr<OmniDirShadowShaderProgram> shader_program;
};
