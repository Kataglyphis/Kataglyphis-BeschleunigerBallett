module;

#include <GLFW/glfw3.h>
#include <glm/geometric.hpp>

#include "../../shared/frontend/CameraState.hpp"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <span>

module kataglyphis.vulkan.camera;

import kataglyphis.shared.frontend.camera_controller;

namespace {

// Kept in the .cpp rather than on Camera: putting it there would drag
// CameraController.ixx's GLFW include into the camera module interface.
auto controllerState(Kataglyphis::Frontend::CameraState &s) -> Kataglyphis::Frontend::CameraControllerState
{
    return { .position = s.position,
        .front = s.front,
        .world_up = s.world_up,
        .right = s.right,
        .up = s.up,
        .yaw = s.yaw,
        .pitch = s.pitch,
        .movement_speed = s.movement_speed,
        .turn_speed = s.turn_speed };
}

}// namespace

// Debug starts on the Dinosaurs scene (see SceneConfig::getModelFile), which
// spans x/z [-10,10] with figures up to y=3.64 on their own ground plane.
// These defaults put the camera outside it, looking slightly down, so the
// cascaded shadows on the floor are visible the moment the app opens.
//
// far_plane matters as much as the position here: cascade splits are computed
// as farPlane * (i / numCascades), so a 4000-unit far plane put the first
// cascade at 1333 units and spread a 2048x2048 shadow map across it - a
// 20-unit scene then landed on a handful of texels. 150 keeps cascade 0 at
// ~50 units, which is tight enough for crisp shadows while still allowing
// some flying around.
Camera::Camera()
  :
#if NDEBUG
    // Release loads crytek-sponza, which needs the wide-open defaults.
    camera_state{ .position = glm::vec3(0.0F, 2.0F, 0.0F),
        .front = {},
        .world_up = glm::vec3(0.0F, 1.0F, 0.0F),
        .right = {},
        .up = {},
        .yaw = -90.F,
        .pitch = 0.0F,
        .movement_speed = 10.F,
        .turn_speed = 0.25F,
        .near_plane = 0.1F,
        .far_plane = 4000.F,
        .fov = 45.F }
#else
    camera_state{ .position = glm::vec3(0.0F, 6.0F, 26.0F),
        .front = {},
        .world_up = glm::vec3(0.0F, 1.0F, 0.0F),
        .right = {},
        .up = {},
        .yaw = -90.F,
        .pitch = -10.0F,
        .movement_speed = 10.F,
        .turn_speed = 0.25F,
        .near_plane = 0.1F,
        .far_plane = 150.F,
        .fov = 45.F }
#endif
{
    update();
}

void Camera::key_control(std::span<const bool> keys, float delta_time)
{
    Kataglyphis::Frontend::apply_keyboard_input(controllerState(camera_state), keys, delta_time);
    update();
}

void Camera::mouse_control(float x_change, float y_change)
{
    Kataglyphis::Frontend::apply_mouse_input(controllerState(camera_state), x_change, y_change);
}

void Camera::set_near_plane(float near_plane) { camera_state.near_plane = near_plane; }

void Camera::set_far_plane(float far_plane) { camera_state.far_plane = far_plane; }

void Camera::set_fov(float fov) { camera_state.fov = fov; }

void Camera::set_camera_position(glm::vec3 new_camera_position) { camera_state.position = new_camera_position; }

auto Camera::calculate_viewmatrix() -> glm::mat4
{
    // very necessary for further calc
    return glm::lookAt(camera_state.position, camera_state.position + camera_state.front, camera_state.world_up);
}

Camera::~Camera() = default;

void Camera::update() { Kataglyphis::Frontend::update_camera_vectors(controllerState(camera_state)); }
