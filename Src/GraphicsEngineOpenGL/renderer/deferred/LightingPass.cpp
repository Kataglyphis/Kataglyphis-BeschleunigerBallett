#include "renderer/deferred/LightingPass.hpp"
#include "scene/atmospheric_effects/clouds/Clouds.hpp"
#include "scene/ObjMaterial.hpp"

#include <time.h>
#include <cassert>
#include <chrono>
#include <ctime>
#include <sstream>

LightingPass::LightingPass()
    :

      current_offset(glm::vec3(0.0f)),
      quad()

{
  create_shader_program();
}

void LightingPass::execute(glm::mat4 projection_matrix,
                           std::shared_ptr<Camera> main_camera,
                           std::shared_ptr<Scene> scene,
                           std::shared_ptr<GBuffer> gbuffer, float delta_time) {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  std::shared_ptr<DirectionalLight> main_light = scene->get_sun();
  std::shared_ptr<Clouds> cloud = scene->get_clouds();
  std::vector<std::shared_ptr<PointLight>> point_lights =
      scene->get_point_lights();

  shader_program->use_shader_program();

  set_uniforms(projection_matrix, main_camera, scene, gbuffer, delta_time);

  // bind textures to their units
  main_light->get_shadow_map()->read(D_LIGHT_SHADOW_TEXTURES_SLOT);
  cloud->read();

  for (uint32_t i = 0; i < static_cast<GLuint>(point_lights.size()); i++) {
    point_lights[i]->get_omni_shadow_map()->read(P_LIGHT_SHADOW_TEXTURES_SLOT +
                                                 i);
  }

  // render screen filling quad
  quad.render();

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void LightingPass::create_shader_program() {
  shader_program =
      std::make_shared<LightingPassShaderProgram>(LightingPassShaderProgram{});
  shader_program->create_from_files("rasterizer/g_buffer_lighting_pass.vert",
                                    "rasterizer/g_buffer_lighting_pass.frag");
}

void LightingPass::set_uniforms(glm::mat4 projection_matrix,
                                std::shared_ptr<Camera> main_camera,
                                std::shared_ptr<Scene> scene,
                                std::shared_ptr<GBuffer> gbuffer,
                                float delta_time) {
  // VP
  glm::mat4 view_matrix = main_camera->get_viewmatrix();
  shader_program->setUniformMatrix4fv(view_matrix, "view");
  shader_program->setUniformMatrix4fv(projection_matrix, "projection");

  // SUN UNIFORMS
  std::shared_ptr<DirectionalLight> main_light = scene->get_sun();
  shader_program->setUniformFloat(main_light->get_radiance(),
                                  "directional_light.base.radiance");
  shader_program->setUniformVec3(main_light->get_color(),
                                 "directional_light.base.color");
  shader_program->setUniformVec3(main_light->get_direction(),
                                 "directional_light.direction");

  // EVERYTHING REGARDING THE SHADOW CASCADE
  shader_program->setUniformInt(D_LIGHT_SHADOW_TEXTURES_SLOT,
                                "directional_shadow_maps");

  std::vector<GLfloat> cascade_slots = main_light->get_cascaded_slots();

  std::stringstream ss;
  for (uint32_t i = 0; i < NUM_CASCADES; i++) {
    glm::vec4 clip_end_slot =
        projection_matrix * glm::vec4(0.0f, 0.0f, -cascade_slots[i + 1], 1.0f);
    ss << "cascade_endpoints[" << i << "]";
    shader_program->setUniformFloat(clip_end_slot.z, ss.str());
    ss.clear();
    ss.str(std::string());
  }

  shader_program->setUniformInt(main_light->get_shadow_map()->get_pcf_radius(),
                                "pcf_radius");

  // READ GBUFFER
  gbuffer->read(shader_program);

  // POINT LIGHTS
  std::vector<std::shared_ptr<PointLight>> point_lights =
      scene->get_point_lights();

  shader_program->setUniformInt(static_cast<uint32_t>(point_lights.size()),
                                "point_light_count");

  for (uint32_t i = 0; i < static_cast<uint32_t>(point_lights.size()); i++) {
    ss << "point_lights[" << i << "].base.color";
    shader_program->setUniformVec3(point_lights[i]->get_color(), ss.str());
    ss.clear();
    ss.str(std::string());

    ss << "point_lights[" << i << "].base.radiance";
    shader_program->setUniformFloat(point_lights[i]->get_radiance(), ss.str());
    ss.clear();
    ss.str(std::string());

    ss << "point_lights[" << i << "].position";
    shader_program->setUniformVec3(point_lights[i]->get_position(), ss.str());
    ss.clear();
    ss.str(std::string());

    ss << "point_lights[" << i << "].base.constant";
    shader_program->setUniformFloat(point_lights[i]->get_constant_factor(),
                                    ss.str());
    ss.clear();
    ss.str(std::string());

    ss << "point_lights[" << i << "].linear";
    shader_program->setUniformFloat(point_lights[i]->get_linear_factor(),
                                    ss.str());
    ss.clear();
    ss.str(std::string());

    ss << "point_lights[" << i << "].exponent";
    shader_program->setUniformFloat(point_lights[i]->get_exponent_factor(),
                                    ss.str());
    ss.clear();
    ss.str(std::string());

    ss << "omni_shadow_maps[" << i << "].shadow_map";
    shader_program->setUniformInt((GLint)(P_LIGHT_SHADOW_TEXTURES_SLOT + i),
                                  ss.str());
    ss.clear();
    ss.str(std::string());

    ss << "omni_shadow_maps[" << i << "]far_plane";
    shader_program->setUniformFloat(point_lights[i]->get_far_plane(), ss.str());
    ss.clear();
    ss.str(std::string());
  }

  // CAMERA
  glm::vec3 camera_position = main_camera->get_camera_position();
  shader_program->setUniformVec3(camera_position, "eye_position");

  // MATERIALS
  std::vector<ObjMaterial> materials = scene->get_materials();
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

  // CLOUDS

  std::shared_ptr<Clouds> cloud = scene->get_clouds();

  shader_program->setUniformVec3(cloud->get_radius(), "cloud.radius");
  GLfloat velocity = cloud->get_movement_speed() * delta_time;
  current_offset = current_offset + cloud->get_movement_direction() * velocity;
  shader_program->setUniformVec3(current_offset, "cloud.offset");
  shader_program->setUniformMatrix4fv(cloud->get_model(),
                                      "cloud.model_to_world");
  shader_program->setUniformFloat(cloud->get_scale(), "cloud.scale");
  shader_program->setUniformFloat(1.f - cloud->get_density(),
                                  "cloud.threshold");
  shader_program->setUniformFloat(cloud->get_pillowness(), "cloud.pillowness");
  shader_program->setUniformFloat(cloud->get_cirrus_effect(),
                                  "cloud.cirrus_effect");
  shader_program->setUniformInt(cloud->get_num_march_steps(),
                                "cloud.num_march_steps");
  shader_program->setUniformInt(cloud->get_num_march_steps_to_light(),
                                "cloud.num_march_steps_to_light");

  if (cloud->get_powder_effect()) {
    shader_program->setUniformInt(true, "cloud.powder_effect");
  } else {
    shader_program->setUniformInt(false, "cloud.powder_effect");
  }

  shader_program->setUniformBlockBinding(UNIFORM_LIGHT_MATRICES_BINDING,
                                         "LightSpaceMatrices");

  shader_program->setUniformInt(RANDOM_NUMBERS_SLOT, "random_number");

  shader_program->setUniformInt(NOISE_128D_TEXTURES_SLOT, "noise_texture_1");
  shader_program->setUniformInt(NOISE_32D_TEXTURES_SLOT, "noise_texture_2");

  shader_program->validate_program();
}

LightingPass::~LightingPass() {}
