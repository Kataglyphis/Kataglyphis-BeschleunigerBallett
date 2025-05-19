#include "gui/GUI.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <filesystem>
#include <sstream>

#include "scene/atmospheric_effects/clouds/Clouds.hpp"
#include "renderer/OpenGLRendererConfig.hpp"
#include "scene/texture/RepeatMode.hpp"
#include "hostDevice/host_device_shared.hpp"

GUI::GUI() {
  // give some arbitrary values; we will update these values after 1 frame :)
  this->direcional_light_radiance = 4.0f;

  this->directional_light_color[0] = 1;
  this->directional_light_color[1] = 1;
  this->directional_light_color[2] = 1;

  this->directional_light_direction[0] = -0.1f;
  this->directional_light_direction[1] = -1.f;
  this->directional_light_direction[2] = -0.1f;

  this->cloud_speed = 6;
  this->cloud_scale = 0.63f;
  this->cloud_density = 0.493f;
  this->cloud_pillowness = 0.966f;
  this->cloud_cirrus_effect = 0.034f;

  this->cloud_mesh_scale[0] = 1000.f;
  this->cloud_mesh_scale[1] = 5.f;
  this->cloud_mesh_scale[2] = 1000.f;

  this->cloud_mesh_offset[0] = -.364f;
  this->cloud_mesh_offset[1] = 367.f;
  this->cloud_mesh_offset[2] = -18.351f;

  this->cloud_powder_effect = true;

  this->cloud_movement_direction[0] = 1.f;
  this->cloud_movement_direction[1] = 1.f;
  this->cloud_movement_direction[2] = 1.f;

  this->cloud_num_march_steps = 8;
  this->cloud_num_march_steps_to_light = 3;

  this->shadow_map_res_index = 3;
  this->shadow_resolution_changed = false;
  this->num_shadow_cascades = NUM_CASCADES;
  this->pcf_radius = 2;
  this->cascaded_shadow_intensity = 0.65f;

  this->available_shadow_map_resolutions[0] = "512";
  this->available_shadow_map_resolutions[1] = "1024";
  this->available_shadow_map_resolutions[2] = "2048";
  this->available_shadow_map_resolutions[3] = "4096";

  std::stringstream texture_base_dir;
  std::filesystem::path cwd = std::filesystem::current_path();
  texture_base_dir << cwd.string();
  texture_base_dir << RELATIVE_RESOURCE_PATH;
  texture_base_dir << "Textures/";

  std::stringstream texture_logo;
  texture_logo << texture_base_dir.str() << "Loading_Screen/Engine_logo.png";
  logo_tex =
      Texture(texture_logo.str().c_str(), std::make_shared<RepeatMode>());
  logo_tex.load_texture_with_alpha_channel();
}

void GUI::init(std::shared_ptr<Window> main_window) {
  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  // ImGuiIO& io = ImGui::GetIO();
  const ImGuiStyle& style = ImGui::GetStyle();
  // Setup Platform/Renderer bindings
  ImGui_ImplGlfw_InitForOpenGL(main_window->get_window(), false);
  const char* glsl_version = "#version 460";
  ImGui_ImplOpenGL3_Init(glsl_version);
  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1);
}

void GUI::render(bool loading_in_progress, float progress,
                 bool& shader_hot_reload_triggered) {
  // feed inputs to dear imgui, start new frame
  // UI.start_new_frame();
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  // render your GUI
  ImGui::Begin("GUI v1.3.3");

  if (loading_in_progress) {
    ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f));
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::Text("Loading scene");
    ImGui::Separator();
  }

  if (ImGui::CollapsingHeader("Hot shader reload")) {
    if (ImGui::Button("Hot reload ALL shaders!")) {
      shader_hot_reload_triggered = true;
    }
  }

  ImGui::Separator();

  if (ImGui::CollapsingHeader("Graphic Settings")) {
    if (ImGui::TreeNode("Directional Light")) {
      ImGui::Separator();
      ImGui::SliderFloat("Radiance", &direcional_light_radiance, 0.0f, 50.0f);
      ImGui::Separator();
      // Edit a color (stored as ~4 floats)
      ImGui::ColorEdit3("Directional Light Color", directional_light_color);
      ImGui::Separator();
      ImGui::SliderFloat3("Light Direction", directional_light_direction, -1.f,
                          1.0f);

      if (ImGui::TreeNode("Shadows")) {
        int shadow_map_res_index_before = shadow_map_res_index;
        ImGui::Combo("Shadow Map Resolution", &shadow_map_res_index,
                     available_shadow_map_resolutions,
                     IM_ARRAYSIZE(available_shadow_map_resolutions));
        if (shadow_map_res_index_before != shadow_map_res_index)
          shadow_resolution_changed = true;

        int num_cascades_before = num_shadow_cascades;
        ImGui::SliderInt("# cascades", &num_shadow_cascades, NUM_MIN_CASCADES,
                         NUM_CASCADES);
        if (num_cascades_before != num_shadow_cascades)
          shadow_resolution_changed = true;

        ImGui::SliderInt("PCF radius", &pcf_radius, 1, 20);
        ImGui::SliderFloat("Shadow intensity", &cascaded_shadow_intensity, 0.0f,
                           1.0f);

        ImGui::TreePop();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Cloud Settings")) {
      ImGui::SliderInt("Speed", &cloud_speed, 0, 30);
      ImGui::SliderInt("# march steps", &cloud_num_march_steps, 1, 128);
      ImGui::SliderInt("# march steps to light",
                       &cloud_num_march_steps_to_light, 1, 128);
      ImGui::SliderFloat3("Movement Direction", cloud_movement_direction, -10.f,
                          10.0f);
      ImGui::SliderFloat("Illumination intensity", &cloud_scale, 0.f, 1.0f);
      ImGui::SliderFloat("Density", &cloud_density, 0.f, 1.0f);
      ImGui::SliderFloat("Pillowness", &cloud_pillowness, 0.f, 1.0f);
      ImGui::SliderFloat("Cirrus effect", &cloud_cirrus_effect, 0.f, 1.0f);
      ImGui::Checkbox("Powder effect", &cloud_powder_effect);
      ImGui::SliderFloat3("Scale", cloud_mesh_scale, 0.f, 1000.0f);
      ImGui::SliderFloat3("Translation", cloud_mesh_offset, -200.f, 400.0f);

      ImGui::TreePop();
    }
  }

  ImGui::Separator();

  /*if (ImGui::CollapsingHeader("Audio Settings")) {

        ImGui::SliderFloat("Volume", &sound_volume, 0.0f, 1.0f);

    }*/

  ImGui::Separator();

  if (ImGui::CollapsingHeader("GUI Settings")) {
    ImGuiStyle& style = ImGui::GetStyle();
    if (ImGui::SliderFloat("Frame Rounding", &style.FrameRounding, 0.0f, 12.0f,
                           "%.0f")) {
      style.GrabRounding = style.FrameRounding;  // Make GrabRounding always the
                                                 // same value as FrameRounding
    }
    {
      bool border = (style.FrameBorderSize > 0.0f);
      if (ImGui::Checkbox("FrameBorder", &border)) {
        style.FrameBorderSize = border ? 1.0f : 0.0f;
      }
    }
    ImGui::SliderFloat("WindowRounding", &style.WindowRounding, 0.0f, 12.0f,
                       "%.0f");
  }

  ImGui::Separator();

  if (ImGui::CollapsingHeader("KEY Bindings")) {
    ImGui::Text(
        "WASD for moving Forward, backward and to the side\n QE for rotating ");
  }

  ImGui::Separator();

  ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
              1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
  // ImGui::ShowDemoWindow();
  // (void*)(intptr_t)
  ImGui::Image(logo_tex.get_id(), ImVec2(200, 200), ImVec2(0, 1), ImVec2(1, 0));

  ImGui::End();

  // feed inputs to dear imgui, start new frame
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void GUI::update_user_input(std::shared_ptr<Scene> scene) {
  std::shared_ptr<DirectionalLight> main_light = scene->get_sun();
  main_light->set_radiance(direcional_light_radiance);
  main_light->get_shadow_map()->set_intensity(cascaded_shadow_intensity);
  main_light->get_shadow_map()->set_pcf_radius(pcf_radius);

  glm::vec3 new_main_light_color(directional_light_color[0],
                                 directional_light_color[1],
                                 directional_light_color[2]);

  main_light->set_color(new_main_light_color);

  glm::vec3 new_main_light_pos(directional_light_direction[0],
                               directional_light_direction[1],
                               directional_light_direction[2]);

  main_light->set_direction(new_main_light_pos);

  glm::vec3 cloud_move(cloud_movement_direction[0], cloud_movement_direction[1],
                       cloud_movement_direction[2]);

  std::shared_ptr<Clouds> clouds = scene->get_clouds();

  clouds->set_movement_direction(cloud_move);
  clouds->set_movement_speed(static_cast<float>(cloud_speed));
  clouds->set_density(cloud_density);
  clouds->set_scale(cloud_scale);
  clouds->set_pillowness(cloud_pillowness);
  clouds->set_cirrus_effect(cloud_cirrus_effect);
  clouds->set_powder_effect(cloud_powder_effect);

  clouds->set_scale(
      glm::vec3(cloud_mesh_scale[0], cloud_mesh_scale[1], cloud_mesh_scale[2]));

  clouds->set_translation(glm::vec3(cloud_mesh_offset[0], cloud_mesh_offset[1],
                                    cloud_mesh_offset[2]));

  GLfloat shadow_map_resolution = 4096.f;

  if (shadow_resolution_changed) {
    switch (shadow_map_res_index) {
      case 0:
        shadow_map_resolution = 512.f;
        break;
      case 1:
        shadow_map_resolution = 1024.f;
        break;
      case 2:
        shadow_map_resolution = 2048.f;
        break;
      case 3:
        shadow_map_resolution = 4096.f;
    }

    main_light->update_shadow_map(shadow_map_resolution, shadow_map_resolution,
                                  NUM_CASCADES);

    shadow_resolution_changed = false;
  }
}

GUI::~GUI() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}
