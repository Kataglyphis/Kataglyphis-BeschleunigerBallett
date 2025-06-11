#include "scene/light/directional_light/DirectionalShadowMapPass.hpp"

#include <memory>

DirectionalShadowMapPass::DirectionalShadowMapPass() { create_shader_program(); }

void DirectionalShadowMapPass::execute(glm::mat4 projection,
  std::shared_ptr<Camera> main_camera,
  GLuint window_width,
  GLuint window_height,
  std::shared_ptr<Scene> scene)
{
    std::shared_ptr<DirectionalLight> sun = scene->get_sun();
    // retreive shadow map before our geometry pass
    sun->calc_orthogonal_projections(
      main_camera->get_viewmatrix(), main_camera->get_fov(), window_width, window_height, NUM_CASCADES);

    shader_program->use_shader_program();

    sun->get_shadow_map()->write();

    glViewport(0, 0, sun->get_shadow_map()->get_shadow_width(), sun->get_shadow_map()->get_shadow_height());

    glClear(GL_DEPTH_BUFFER_BIT);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // glCullFace(GL_FRONT); // avoid peter panning
    sun->get_shadow_map()->write_light_matrices(sun->get_cascaded_light_matrices());

    shader_program->setUniformBlockBinding(UNIFORM_LIGHT_MATRICES_BINDING, "LightSpaceMatrices");

    std::vector<std::shared_ptr<GameObject>> game_objects = scene->get_game_objects();

    for (std::shared_ptr<GameObject> object : game_objects) {
        /* if (object_is_visible(object)) {*/

        set_game_object_uniforms(object->get_world_trafo(), object->get_normal_world_trafo());

        object->render();
        //}
    }

    // glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    shader_program->validate_program();
}

void DirectionalShadowMapPass::create_shader_program()
{
    shader_program = std::make_shared<ShaderProgram>(ShaderProgram{});
    shader_program->create_from_files("rasterizer/shadows/directional_shadow_map.vert",
      "rasterizer/shadows/directional_shadow_map.geom",
      "rasterizer/shadows/directional_shadow_map.frag");
}

void DirectionalShadowMapPass::set_game_object_uniforms(glm::mat4 model, glm::mat4 normal_model)
{
    // DO NOT set neither normal model nor material_id hence we didn't need it
    shader_program->setUniformMatrix4fv(model, "model");
}

DirectionalShadowMapPass::~DirectionalShadowMapPass() {}
