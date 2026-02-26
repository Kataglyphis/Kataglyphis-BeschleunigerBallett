#ifndef KATAGLYPHIS_SHARED_FRONTEND_CAMERA_STATE_HPP
#define KATAGLYPHIS_SHARED_FRONTEND_CAMERA_STATE_HPP

#include <glm/glm.hpp>

namespace Kataglyphis::Frontend {
struct CameraState
{
    glm::vec3 position{};
    glm::vec3 front{};
    glm::vec3 world_up{};
    glm::vec3 right{};
    glm::vec3 up{};

    float yaw{};
    float pitch{};

    float movement_speed{};
    float turn_speed{};

    float near_plane{};
    float far_plane{};
    float fov{};
};
}// namespace Kataglyphis::Frontend

#endif