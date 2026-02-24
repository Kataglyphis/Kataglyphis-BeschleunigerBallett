module;

#include <glad/glad.h>
#include <glm/glm.hpp>

export module kataglyphis.opengl.rotation;

export struct Rotation
{
    GLfloat degrees;
    glm::vec3 axis;
};
