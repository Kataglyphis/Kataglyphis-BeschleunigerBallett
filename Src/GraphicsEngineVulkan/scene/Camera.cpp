module;

#include <GLFW/glfw3.h>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

module kataglyphis.vulkan.camera;

import kataglyphis.shared.frontend.camera_controller;

Camera::Camera()
  :

    camera_state{ .position = glm::vec3(0.0F, 100.0F, -80.0F),
        .front = glm::vec3(0.0F, 0.0F, -1.F),
        .world_up = glm::vec3(0.0F, 1.0F, 0.0F),
        .right = glm::normalize(glm::cross(glm::vec3(0.0F, 0.0F, -1.F), glm::vec3(0.0F, 1.0F, 0.0F))),
        .up = glm::normalize(
          glm::cross(glm::normalize(glm::cross(glm::vec3(0.0F, 0.0F, -1.F), glm::vec3(0.0F, 1.0F, 0.0F))),
            glm::vec3(0.0F, 0.0F, -1.F))),
        .yaw = 80.F,
        .pitch = -40.0F,
        .movement_speed = 200.F,
        .turn_speed = 0.25F,
        .near_plane = 0.1F,
        .far_plane = 4000.F,
        .fov = 45.F }

{}

void Camera::key_control(const bool *keys, float delta_time)
{
    Kataglyphis::Frontend::apply_keyboard_input({ camera_state.position,
                                                  camera_state.front,
                                                  camera_state.world_up,
                                                  camera_state.right,
                                                  camera_state.up,
                                                  camera_state.yaw,
                                                  camera_state.pitch,
                                                  camera_state.movement_speed,
                                                  camera_state.turn_speed },
      keys,
      delta_time);
}

void Camera::mouse_control(float x_change, float y_change)
{
    Kataglyphis::Frontend::apply_mouse_input({ camera_state.position,
                                               camera_state.front,
                                               camera_state.world_up,
                                               camera_state.right,
                                               camera_state.up,
                                               camera_state.yaw,
                                               camera_state.pitch,
                                               camera_state.movement_speed,
                                               camera_state.turn_speed },
      x_change,
      y_change);
}

void Camera::set_near_plane(float near_plane) { camera_state.near_plane = near_plane; }

void Camera::set_far_plane(float far_plane) { camera_state.far_plane = far_plane; }

void Camera::set_fov(float fov) { camera_state.fov = fov; }

void Camera::set_camera_position(glm::vec3 new_camera_position) { camera_state.position = new_camera_position; }

auto Camera::calculate_viewmatrix() -> glm::mat4
{
    // very necessary for further calc
    return glm::lookAt(camera_state.position, camera_state.position + camera_state.front, camera_state.up);
}

Camera::~Camera() = default;

void Camera::update()
{
    Kataglyphis::Frontend::update_camera_vectors({ camera_state.position,
      camera_state.front,
      camera_state.world_up,
      camera_state.right,
      camera_state.up,
      camera_state.yaw,
      camera_state.pitch,
      camera_state.movement_speed,
      camera_state.turn_speed });
}
