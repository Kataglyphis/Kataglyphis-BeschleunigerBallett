module;

#include <cstdint>
#include <filesystem>
#include <memory>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>
#include <string>

#include "hostDevice/host_device_shared.hpp"
module kataglyphis.opengl.scene;

import kataglyphis.opengl.directional_light;
import kataglyphis.opengl.point_light;
import kataglyphis.opengl.clouds;
import kataglyphis.opengl.window;
import kataglyphis.opengl.view_frustum_culling;
import kataglyphis.opengl.game_object;
import kataglyphis.opengl.obj_material;

Scene::Scene()
  :

    sun(std::make_shared<DirectionalLight>(static_cast<uint32_t>(4096),
      static_cast<uint32_t>(4096),
      1.F,
      1.F,
      1.F,
      1.F,
      -0.1F,
      -0.8F,
      -0.1F,
      main_camera->get_near_plane(),
      main_camera->get_far_plane(),
      NUM_CASCADES)),
    clouds(std::make_shared<Clouds>()),
    view_frustum_culling(std::make_shared<ViewFrustumCulling>(ViewFrustumCulling{})), progress(0.F),
    loaded_scene(false), context_setup(false)
{}

Scene::Scene(const std::shared_ptr<Camera> &main_camera, std::shared_ptr<Window> main_window)
  :

    main_camera(main_camera), sun(std::make_shared<DirectionalLight>(static_cast<uint32_t>(4096),
                                static_cast<uint32_t>(4096),
                                1.F,
                                1.F,
                                1.F,
                                1.F,
                                -0.1F,
                                -0.8F,
                                -0.1F,
                                main_camera->get_near_plane(),
                                main_camera->get_far_plane(),
                                NUM_CASCADES)),
    clouds(std::make_shared<Clouds>()), main_window(std::move(std::move(main_window))),
    view_frustum_culling(std::make_shared<ViewFrustumCulling>(ViewFrustumCulling{})), progress(0.F),
    loaded_scene(false), context_setup(false)

{
    point_lights.reserve(MAX_POINT_LIGHTS);
    point_lights.push_back(std::make_shared<PointLight>(static_cast<uint32_t>(1024),
      static_cast<uint32_t>(1024),
      0.01F,
      100.F,
      0.0F,
      1.0F,
      0.0F,
      1.0F,
      0.0F,
      0.0F,
      0.0F,
      0.1F,
      0.1F,
      0.1F));

    point_lights[0]->set_position(glm::vec3(0.0, -24.F, -24.0));
}

auto Scene::get_point_light_count() const -> GLuint { return static_cast<uint32_t>(point_lights.size()); }

auto Scene::get_sun() -> std::shared_ptr<DirectionalLight> { return sun; }

auto Scene::get_point_lights() const -> std::vector<std::shared_ptr<PointLight>> { return point_lights; }

void Scene::load_models()
{
    glm::vec3 const sponza_offset = glm::vec3(0.F, 0.0F, 0.0F);
    GLfloat const sponza_scale = 10.F;
    Rotation sponza_rot{};
    sponza_rot.degrees = 0.0F;
    sponza_rot.axis = glm::vec3(0.0F, 1.0F, 0.0F);

    glm::vec3 const clouds_offset = glm::vec3(-3.F, 20.0F, -3.0F);
    glm::vec3 const clouds_scale = glm::vec3(1.F, 1.F, 1.F);
    clouds->set_scale(clouds_scale);
    clouds->set_translation(clouds_offset);

    std::stringstream modelFile;
    std::filesystem::path const cwd = std::filesystem::current_path();
    modelFile << cwd.string();
    modelFile << RELATIVE_RESOURCE_PATH << "Models/dinosaurs.obj";
    /*"../Models/Pillum/PilumPainting_Export.obj",*/
    /*"../Models/crytek-sponza/sponza_triag.obj",*/

    std::shared_ptr<GameObject> const sponza =
      std::make_shared<GameObject>(modelFile.str(), sponza_offset, sponza_scale, sponza_rot);
    progress += 1.F;

    game_objects.push_back(sponza);

    mx_isLoaded.lock();
    loaded_scene = true;
    mx_isLoaded.unlock();
}

auto Scene::get_materials() -> std::vector<ObjMaterial> { return game_objects[0]->get_model()->get_materials(); }

auto Scene::is_loaded() -> bool
{
    std::scoped_lock const guard{ mx_isLoaded };
    return loaded_scene;
}

auto Scene::get_progress() -> GLfloat
{
    std::scoped_lock const guard{ mx_progress };
    return progress;
}

void Scene::setup_game_object_context()
{
    game_objects[0]->get_model()->create_render_context();
    context_setup = true;
}

void Scene::bind_textures_and_buffer() { game_objects[0]->get_model()->bind_ressources(); }

void Scene::unbind_textures_and_buffer() { game_objects[0]->get_model()->unbind_resources(); }

auto Scene::get_texture_count(int /*index*/) -> int { return game_objects[0]->get_model()->get_texture_count(); }

void Scene::set_context_setup(bool is_context_setup) { this->context_setup = is_context_setup; }

auto Scene::get_context_setup() const -> bool { return context_setup; }

void Scene::add_game_object(const std::string &model_path, glm::vec3 translation, GLfloat scale, Rotation rot)
{
    game_objects.push_back(std::make_shared<GameObject>(GameObject()));
    game_objects.back()->init(model_path, translation, scale, rot);
}

auto Scene::get_clouds() -> std::shared_ptr<Clouds> { return clouds; }

auto Scene::get_game_objects() const -> std::vector<std::shared_ptr<GameObject>> { return game_objects; }

auto Scene::object_is_visible(const std::shared_ptr<GameObject> &game_object) -> bool
{
    return view_frustum_culling->is_inside(
      static_cast<GLfloat>(main_window->get_buffer_width()) / static_cast<GLfloat>(main_window->get_buffer_height()),
      main_camera,
      game_object->get_aabb(),
      game_object->get_world_trafo());
}

Scene::~Scene() = default;
