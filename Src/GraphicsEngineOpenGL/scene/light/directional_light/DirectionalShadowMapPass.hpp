#pragma once

#include "scene/light/directional_light/DirectionalLight.hpp"
#include "renderer/RenderPassSceneDependend.hpp"
#include "scene/Scene.hpp"
#include "renderer/ShaderProgram.hpp"
#include "scene/ViewFrustumCulling.hpp"

class DirectionalShadowMapPass : public RenderPassSceneDependend {
 public:
  DirectionalShadowMapPass();

  void execute(glm::mat4 projection, std::shared_ptr<Camera> main_camera,
               GLuint window_width, GLuint window_height,
               std::shared_ptr<Scene> scene);

  void create_shader_program();

  void set_game_object_uniforms(glm::mat4 model, glm::mat4 normal_model);

  ~DirectionalShadowMapPass();

 private:
  std::shared_ptr<ShaderProgram> shader_program;
};
