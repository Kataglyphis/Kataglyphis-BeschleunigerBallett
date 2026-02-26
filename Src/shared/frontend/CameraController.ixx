module;

#include <GLFW/glfw3.h>
#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

export module kataglyphis.shared.frontend.camera_controller;

export namespace Kataglyphis::Frontend {

struct CameraControllerState
{
    glm::vec3 &position;
    glm::vec3 &front;
    glm::vec3 const &world_up;
    glm::vec3 &right;
    glm::vec3 &up;
    float &yaw;
    float &pitch;
    float movement_speed;
    float turn_speed;
};

inline void update_camera_vectors(CameraControllerState state)
{
    state.front.x = std::cos(glm::radians(state.yaw)) * std::cos(glm::radians(state.pitch));
    state.front.y = std::sin(glm::radians(state.pitch));
    state.front.z = std::sin(glm::radians(state.yaw)) * std::cos(glm::radians(state.pitch));
    state.front = glm::normalize(state.front);

    state.right = glm::normalize(glm::cross(state.front, state.world_up));
    state.up = glm::normalize(glm::cross(state.right, state.front));
}

inline void apply_keyboard_input(CameraControllerState state, const bool *keys, float delta_time)
{
    float const velocity = state.movement_speed * delta_time;

    if (keys[GLFW_KEY_W]) { state.position += state.front * velocity; }
    if (keys[GLFW_KEY_D]) { state.position += state.right * velocity; }
    if (keys[GLFW_KEY_A]) { state.position += -state.right * velocity; }
    if (keys[GLFW_KEY_S]) { state.position += -state.front * velocity; }
    if (keys[GLFW_KEY_Q]) { state.yaw += -velocity; }
    if (keys[GLFW_KEY_E]) { state.yaw += velocity; }
}

inline void apply_mouse_input(CameraControllerState state, float x_change, float y_change)
{
    x_change *= state.turn_speed;
    y_change *= state.turn_speed;

    state.yaw += x_change;
    state.pitch += y_change;

    state.pitch = std::min(state.pitch, 89.0F);
    state.pitch = std::max(state.pitch, -89.0F);

    update_camera_vectors(state);
}

}// namespace Kataglyphis::Frontend
