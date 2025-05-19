#pragma once
//#include <glad/glad.h>
//#include <GLFW/glfw3.h>

#include <memory>

#include "scene/Scene.hpp"
#include "scene/texture/Texture.hpp"
#include "window/Window.hpp"

class GUI {
 public:
  GUI();

  void init(std::shared_ptr<Window> main_window);

  void render(bool loading_in_progress, float progress,
              bool& shader_hot_reload_triggered);

  void update_user_input(std::shared_ptr<Scene> scene);

  ~GUI();

 private:
  Texture logo_tex;

  float direcional_light_radiance;
  float directional_light_color[3];
  float directional_light_direction[3];

  int cloud_speed;
  float cloud_scale;
  float cloud_density;
  float cloud_pillowness;
  float cloud_cirrus_effect;
  float cloud_mesh_scale[3];
  float cloud_mesh_offset[3];
  bool cloud_powder_effect;
  float cloud_movement_direction[3];
  int cloud_num_march_steps;
  int cloud_num_march_steps_to_light;

  int shadow_map_res_index;
  bool shadow_resolution_changed;
  int num_shadow_cascades;
  int pcf_radius;
  float cascaded_shadow_intensity;
  const char* available_shadow_map_resolutions[4];
};
