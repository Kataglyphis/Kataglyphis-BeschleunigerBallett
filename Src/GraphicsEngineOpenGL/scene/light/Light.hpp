#pragma once
#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Light
{
  public:
    Light();

    Light(GLfloat red, GLfloat green, GLfloat blue, GLfloat radiance);

    glm::vec3 get_color() const { return color; };
    float get_radiance() const { return radiance; };

    ~Light();

  protected:
    glm::vec3 color;
    float radiance;

    glm::mat4 light_proj;
};
