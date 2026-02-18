#include "scene/light/Light.hpp"
#include <glm/ext/vector_float3.hpp>
#include <glad/glad.h>

Light::Light()
  :

    color(glm::vec3(1.0F)), radiance(1.0F)

{}

Light::Light(GLfloat red, GLfloat green, GLfloat blue, GLfloat radiance)
  :

    color(glm::vec3(red, green, blue)), radiance(radiance)

{}

Light::~Light() = default;
