#include "scene/light/Light.hpp"

Light::Light()
  :

    color(glm::vec3(1.0f)), radiance(1.0f)

{}

Light::Light(GLfloat red, GLfloat green, GLfloat blue, GLfloat radiance)
  :

    color(glm::vec3(red, green, blue)), radiance(radiance)

{}

Light::~Light() {}
