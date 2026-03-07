module;

#include <algorithm>

#include <glm/geometric.hpp>
#include "GLFW/glfw3.h"
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <cmath>
#include <glm/trigonometric.hpp>

module kataglyphis.opengl.camera;

import kataglyphis.shared.frontend.camera_controller;

glm::vec3 Camera::get_camera_direction() const { return glm::normalize(camera_state.front); }

Camera::Camera()
  :

    camera_state{ .position = glm::vec3(0.0F, 50.0F, 0.0F),
        // here we want the normal coord. axis z is showing to us !!
        .front = glm::vec3(0.0F, 0.0F, -1.0F),
        .world_up = glm::vec3(0.0F, 1.0F, 0.0F),
        .right = glm::normalize(glm::cross(glm::vec3(0.0F, 0.0F, -1.0F), glm::vec3(0.0F, 1.0F, 0.0F))),
        .up = glm::normalize(
          glm::cross(glm::normalize(glm::cross(glm::vec3(0.0F, 0.0F, -1.0F), glm::vec3(0.0F, 1.0F, 0.0F))),
            glm::vec3(0.0F, 0.0F, -1.0F))),
        .yaw = -60.0F,
        .pitch = 0.0F,
        .movement_speed = 35.0F,
        .turn_speed = 0.25F,
        .near_plane = 0.1F,
        .far_plane = 1000.F,
        .fov = 45.F }

{}

Camera::Camera(glm::vec3 start_position,
  glm::vec3 start_up,
  float start_yaw,
  float start_pitch,
  float start_move_speed,
  float start_turn_speed,
  float near_plane,
  float far_plane,
  float fov)
  :

    camera_state{ .position = start_position,
        // here we want the normal coord. axis z is showing to us !!
        .front = glm::vec3(0.0F, 0.0F, -1.0F),
        .world_up = start_up,
        .right = glm::normalize(glm::cross(glm::vec3(0.0F, 0.0F, -1.0F), start_up)),
        .up = glm::normalize(
          glm::cross(glm::normalize(glm::cross(glm::vec3(0.0F, 0.0F, -1.0F), start_up)), glm::vec3(0.0F, 0.0F, -1.0F))),
        .yaw = start_yaw,
        .pitch = start_pitch,
        .movement_speed = start_move_speed,
        .turn_speed = start_turn_speed,
        .near_plane = near_plane,
        .far_plane = far_plane,
        .fov = fov }

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

auto Camera::get_viewmatrix() const -> glm::mat4
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
