module;

#include <glm/vec3.hpp>

export module kataglyphis.opengl.rotation;

export struct Rotation
{
    float degrees;
    glm::vec3 axis;
};
