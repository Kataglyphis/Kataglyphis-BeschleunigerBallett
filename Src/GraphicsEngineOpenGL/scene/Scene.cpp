#include "scene/Scene.hpp"

#include "renderer/OpenGLRendererConfig.hpp"
#include <sstream>
#include <filesystem>

Scene::Scene()
    :

      main_camera(),
      sun(std::make_shared<DirectionalLight>(
          static_cast<uint32_t>(4096), static_cast<uint32_t>(4096), 1.f, 1.f,
          1.f, 1.f, -0.1f, -0.8f, -0.1f, main_camera->get_near_plane(),
          main_camera->get_far_plane(), NUM_CASCADES)),
      clouds(std::make_shared<Clouds>()),
      main_window(),
      view_frustum_culling(
          std::make_shared<ViewFrustumCulling>(ViewFrustumCulling{})),
      progress(0.f),
      loaded_scene(false),
      context_setup(false) {}

Scene::Scene(std::shared_ptr<Camera> main_camera,
             std::shared_ptr<Window> main_window)
    :

      main_camera(main_camera),
      sun(std::make_shared<DirectionalLight>(
          static_cast<uint32_t>(4096), static_cast<uint32_t>(4096), 1.f, 1.f,
          1.f, 1.f, -0.1f, -0.8f, -0.1f, main_camera->get_near_plane(),
          main_camera->get_far_plane(), NUM_CASCADES)),
      clouds(std::make_shared<Clouds>()),
      main_window(main_window),
      view_frustum_culling(
          std::make_shared<ViewFrustumCulling>(ViewFrustumCulling{})),
      progress(0.f),
      loaded_scene(false),
      context_setup(false)

{
  point_lights.reserve(MAX_POINT_LIGHTS);
  point_lights.push_back(std::make_shared<PointLight>(
      static_cast<uint32_t>(1024), static_cast<uint32_t>(1024), 0.01f, 100.f,
      0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.1f, 0.1f, 0.1f));

  point_lights[0]->set_position(glm::vec3(0.0, -24.f, -24.0));
}

GLuint Scene::get_point_light_count() const {
  return static_cast<uint32_t>(point_lights.size());
}

std::shared_ptr<DirectionalLight> Scene::get_sun() { return sun; }

std::vector<std::shared_ptr<PointLight>> Scene::get_point_lights() const {
  return point_lights;
}

void Scene::load_models() {
  glm::vec3 sponza_offset = glm::vec3(0.f, 0.0f, 0.0f);
  GLfloat sponza_scale = 10.f;
  Rotation sponza_rot;
  sponza_rot.degrees = 0.0f;
  sponza_rot.axis = glm::vec3(0.0f, 1.0f, 0.0f);

  glm::vec3 clouds_offset = glm::vec3(-3.f, 20.0f, -3.0f);
  glm::vec3 clouds_scale = glm::vec3(1.f, 1.f, 1.f);
  clouds->set_scale(clouds_scale);
  clouds->set_translation(clouds_offset);

  std::stringstream modelFile;
  std::filesystem::path cwd = std::filesystem::current_path();
  modelFile << cwd.string();
  modelFile << RELATIVE_RESOURCE_PATH << "Models/dinosaurs.obj";
  /*"../Models/Pillum/PilumPainting_Export.obj",*/
  /*"../Models/crytek-sponza/sponza_triag.obj",*/

  std::shared_ptr<GameObject> sponza = std::make_shared<GameObject>(
      modelFile.str(), sponza_offset, sponza_scale, sponza_rot);
  progress += 1.f;

  game_objects.push_back(sponza);

  mx_isLoaded.lock();
  loaded_scene = true;
  mx_isLoaded.unlock();
}

std::vector<ObjMaterial> Scene::get_materials() {
  return game_objects[0]->get_model()->get_materials();
}

bool Scene::is_loaded() {
  std::lock_guard<std::mutex> guard{mx_isLoaded};
  return loaded_scene;
}

GLfloat Scene::get_progress() {
  std::lock_guard<std::mutex> guard{mx_progress};
  return progress;
}

void Scene::setup_game_object_context() {
  game_objects[0]->get_model()->create_render_context();
  context_setup = true;
}

void Scene::bind_textures_and_buffer() {
  game_objects[0]->get_model()->bind_ressources();
}

void Scene::unbind_textures_and_buffer() {
  game_objects[0]->get_model()->unbind_resources();
}

int Scene::get_texture_count(int index) {
  return game_objects[0]->get_model()->get_texture_count();
}

void Scene::set_context_setup(bool context_setup) {
  this->context_setup = context_setup;
}

bool Scene::get_context_setup() const { return context_setup; }

void Scene::add_game_object(const std::string& model_path,
                            glm::vec3 translation, GLfloat scale,
                            Rotation rot) {
  game_objects.push_back(std::make_shared<GameObject>(GameObject()));
  game_objects.back()->init(model_path, translation, scale, rot);
}

std::shared_ptr<Clouds> Scene::get_clouds() { return clouds; }

std::vector<std::shared_ptr<GameObject>> Scene::get_game_objects() const {
  return game_objects;
}

bool Scene::object_is_visible(std::shared_ptr<GameObject> game_object) {
  return view_frustum_culling->is_inside(
      static_cast<GLfloat>(main_window->get_buffer_width()) /
          static_cast<GLfloat>(main_window->get_buffer_height()),
      main_camera, game_object->get_aabb(), game_object->get_world_trafo());
}

Scene::~Scene() {}
