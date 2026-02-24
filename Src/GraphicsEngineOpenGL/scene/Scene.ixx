module;

#include <mutex>
#include <thread>
#include <vector>
#include <memory>
#include <string>
#include <glm/glm.hpp>
#include <glad/glad.h>

export module kataglyphis.opengl.scene;

import kataglyphis.opengl.camera;
import kataglyphis.opengl.rotation;
import kataglyphis.opengl.directional_light;
import kataglyphis.opengl.point_light;
import kataglyphis.opengl.game_object;
import kataglyphis.opengl.obj_material;
import kataglyphis.opengl.view_frustum_culling;
import kataglyphis.opengl.clouds;
import kataglyphis.opengl.window;

export class Scene
{
  public:
    Scene();
    Scene(const std::shared_ptr<Camera> &main_camera, std::shared_ptr<Window> main_window);

    std::thread spwan()
    {
        return std::thread([this] { load_models(); });
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
    bool object_is_visible(const std::shared_ptr<GameObject> &game_object);

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
