#pragma once
#include <mutex>
#include <thread>
#include <vector>

#include "GameObject.hpp"
#include "scene/ObjMaterial.hpp"
#include "scene/atmospheric_effects/clouds/Clouds.hpp"
#include "scene/light/directional_light/DirectionalLight.hpp"
#include "scene/light/point_light/PointLight.hpp"
// #include "renderer/RenderPassSceneDependend.hpp"
#include "Rotation.hpp"
#include "scene/ViewFrustumCulling.hpp"
#include "window/Window.hpp"

class Scene
{
  public:
    Scene();
    Scene(std::shared_ptr<Camera> main_camera, std::shared_ptr<Window> main_window);

    std::thread spwan()
    {
        return std::thread([=] { load_models(); });
    }

    GLuint get_point_light_count() const;
    std::shared_ptr<DirectionalLight> get_sun();
    std::vector<std::shared_ptr<PointLight>> get_point_lights() const;
    std::vector<ObjMaterial> get_materials();
    GLfloat get_progress();
    int get_texture_count(int index);
    bool get_context_setup() const;
    std::shared_ptr<Clouds> get_clouds();
    std::vector<std::shared_ptr<GameObject>> get_game_objects() const;

    void add_game_object(const std::string &model_path, glm::vec3 translation, GLfloat scale, Rotation rot);
    void load_models();

    bool is_loaded();
    void setup_game_object_context();

    void bind_textures_and_buffer();
    void unbind_textures_and_buffer();

    void set_context_setup(bool context_setup);

    ~Scene();

  private:
    bool object_is_visible(std::shared_ptr<GameObject> game_object);

    std::shared_ptr<Camera> main_camera;

    // lights
    std::shared_ptr<DirectionalLight> sun;
    std::vector<std::shared_ptr<PointLight>> point_lights;
    std::shared_ptr<Clouds> clouds;

    std::shared_ptr<Window> main_window;
    std::shared_ptr<ViewFrustumCulling> view_frustum_culling;

    std::vector<std::shared_ptr<GameObject>> game_objects;

    GLfloat progress;
    bool loaded_scene;

    std::mutex mx_progress;
    std::mutex mx_isLoaded;
    std::mutex mx_space_ship;

    bool context_setup;
};
