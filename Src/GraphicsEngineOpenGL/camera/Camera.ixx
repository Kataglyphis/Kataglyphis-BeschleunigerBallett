module;

// clang-format off
// you must include glad before glfw!
// therefore disable clang-format for this section


// clang-format on

#include "../../shared/frontend/CameraState.hpp"

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

export module kataglyphis.opengl.camera;

export class Camera
{
  public:
    Camera();
    Camera(const Camera &) = default;
    Camera &operator=(const Camera &) = default;
    Camera(Camera &&) = default;
    Camera &operator=(Camera &&) = default;

    Camera(glm::vec3 start_position,
      glm::vec3 start_up,
      float start_yaw,
      float start_pitch,
      float start_move_speed,
      float start_turn_speed,
      float near_plane,
      float far_plane,
      float fov);

    void key_control(const bool *keys, float delta_time);
    void mouse_control(float x_change, float y_change);

    glm::vec3 get_camera_position() const { return camera_state.position; };
    glm::vec3 get_camera_direction() const;
    glm::vec3 get_up_axis() const { return camera_state.up; };
    glm::vec3 get_right_axis() const { return camera_state.right; };
    float get_near_plane() const { return camera_state.near_plane; };
    float get_far_plane() const { return camera_state.far_plane; };
    float get_fov() const { return camera_state.fov; };
    float get_yaw() const { return camera_state.yaw; };
    glm::mat4 get_viewmatrix() const;

    void set_near_plane(float near_plane);
    void set_far_plane(float far_plane);
    void set_fov(float fov);
    void set_camera_position(glm::vec3 new_camera_position);

    ~Camera();

  private:
    Kataglyphis::Frontend::CameraState camera_state;

    void update();
};
