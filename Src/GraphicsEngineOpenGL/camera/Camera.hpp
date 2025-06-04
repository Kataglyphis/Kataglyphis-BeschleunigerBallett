#pragma once
// clang-format off
// you must include glad before glfw!
// therefore disable clang-format for this section
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera
{
  public:
    Camera();

    Camera(glm::vec3 start_position,
      glm::vec3 start_up,
      float start_yaw,
      float start_pitch,
      float start_move_speed,
      float start_turn_speed,
      float near_plane,
      float far_plane,
      float fov);

    void key_control(bool *keys, float delta_time);
    void mouse_control(float x_change, float y_change);

    glm::vec3 get_camera_position() const { return position; };
    glm::vec3 get_camera_direction() const { return glm::normalize(front); };
    glm::vec3 get_up_axis() const { return up; };
    glm::vec3 get_right_axis() const { return right; };
    float get_near_plane() const { return near_plane; };
    float get_far_plane() const { return far_plane; };
    float get_fov() const { return fov; };
    float get_yaw() const { return yaw; };
    glm::mat4 get_viewmatrix() const;

    void set_near_plane(float near_plane);
    void set_far_plane(float far_plane);
    void set_fov(float fov);
    void set_camera_position(glm::vec3 new_camera_position);

    ~Camera();

  private:
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 world_up;
    glm::vec3 right;
    glm::vec3 up;

    float yaw;
    float pitch;

    float movement_speed;
    float turn_speed;

    float near_plane, far_plane, fov;

    void update();
};
