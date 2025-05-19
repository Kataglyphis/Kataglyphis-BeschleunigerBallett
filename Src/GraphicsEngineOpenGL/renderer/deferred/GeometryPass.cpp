#include "renderer/deferred/GeometryPass.hpp"
#include "scene/atmospheric_effects/clouds/Clouds.hpp"
#include "scene/light/directional_light/DirectionalLight.hpp"
#include "scene/ViewFrustumCulling.hpp"

#include <sstream>
GeometryPass::GeometryPass() : skybox() { create_shader_program(); }

void GeometryPass::execute(glm::mat4 projection_matrix,
                           std::shared_ptr<Camera> main_camera,
                           GLuint window_width, GLuint window_height,
                           GLuint gbuffer_id, GLfloat delta_time,
                           std::shared_ptr<Scene> scene) {
  glBindFramebuffer(GL_FRAMEBUFFER, gbuffer_id);

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glViewport(0, 0, window_width, window_height);

  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);

  shader_program->use_shader_program();

  glm::mat4 view_matrix = main_camera->get_viewmatrix();
  std::vector<ObjMaterial> materials = scene->get_materials();

  shader_program->setUniformMatrix4fv(projection_matrix, "projection");
  shader_program->setUniformMatrix4fv(view_matrix, "view");

  std::stringstream ss;
  for (uint32_t i = 0; i < static_cast<uint32_t>(scene->get_texture_count(0));
       i++) {
    ss << "model_textures[" << i << "]";
    shader_program->setUniformInt(MODEL_TEXTURES_SLOT + i, ss.str());
    ss.clear();
    ss.str(std::string());
  }

  for (uint32_t i = 0; i < static_cast<uint32_t>(materials.size()); i++) {
    ss << "materials[" << i << "].ambient";
    shader_program->setUniformVec3(materials[i].get_ambient(), ss.str());
    ss.clear();
    ss.str(std::string());

    ss << "materials[" << i << "].diffuse";
    shader_program->setUniformVec3(materials[i].get_diffuse(), ss.str());
    ss.clear();
    ss.str(std::string());

    ss << "materials[" << i << "].specular";
    shader_program->setUniformVec3(materials[i].get_specular(), ss.str());
    ss.clear();
    ss.str(std::string());

    ss << "materials[" << i << "].transmittance";
    shader_program->setUniformVec3(materials[i].get_transmittance(), ss.str());
    ss.clear();
    ss.str(std::string());

    ss << "materials[" << i << "].emission";
    shader_program->setUniformVec3(materials[i].get_emission(), ss.str());
    ss.clear();
    ss.str(std::string());

    ss << "materials[" << i << "].shininess";
    shader_program->setUniformFloat(materials[i].get_shininess(), ss.str());
    ss.clear();
    ss.str(std::string());

    ss << "materials[" << i << "].ior";
    shader_program->setUniformFloat(materials[i].get_ior(), ss.str());
    ss.clear();
    ss.str(std::string());

    ss << "materials[" << i << "].dissolve";
    shader_program->setUniformFloat(materials[i].get_dissolve(), ss.str());
    ss.clear();
    ss.str(std::string());

    ss << "materials[" << i << "].illum";
    shader_program->setUniformInt(materials[i].get_illum(), ss.str());
    ss.clear();
    ss.str(std::string());

    ss << "materials[" << i << "].textureID";
    shader_program->setUniformInt(materials[i].get_textureID(), ss.str());
    ss.clear();
    ss.str(std::string());
  }

  shader_program->validate_program();

  scene->bind_textures_and_buffer();

  // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  // aabb->render();
  // glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  //

  std::vector<std::shared_ptr<GameObject>> game_objects =
      scene->get_game_objects();

  for (std::shared_ptr<GameObject> object : game_objects) {
    /* if (object_is_visible(object)) {*/

    set_game_object_uniforms(object->get_world_trafo(),
                             object->get_normal_world_trafo());

    object->render();
    //}
  }

  skybox.draw_sky_box(projection_matrix, view_matrix, window_width,
                      window_height, delta_time);

  /*glCullFace(GL_FRONT);
    glFrontFace(GL_CCW);*/
  // render the AABB for the clouds
  glDisable(GL_CULL_FACE);
  std::shared_ptr<Clouds> clouds = scene->get_clouds();
  clouds->render(projection_matrix, view_matrix, window_width, window_height);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GeometryPass::create_shader_program() {
  this->shader_program =
      std::make_shared<GeometryPassShaderProgram>(GeometryPassShaderProgram{});
  this->shader_program->create_from_files(
      "rasterizer/g_buffer_geometry_pass.vert",
      "rasterizer/g_buffer_geometry_pass.frag");
}

void GeometryPass::set_game_object_uniforms(glm::mat4 model,
                                            glm::mat4 normal_model) {
  shader_program->setUniformMatrix4fv(model, "model");
  shader_program->setUniformMatrix4fv(normal_model, "normal_model");
}

GeometryPass::~GeometryPass() {}
