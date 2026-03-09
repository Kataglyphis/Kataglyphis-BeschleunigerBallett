module;

#include <vector>
#include <memory>
#include <glad/glad.h>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/trigonometric.hpp>
#include <glm/ext/matrix_transform.hpp>

module kataglyphis.opengl.point_light;

import kataglyphis.opengl.point_light.omni_dir_shadow_map;

PointLight::PointLight()
  :

    position(glm::vec3(0.0F)), constant(1.0F), linear(0.0F), exponent(0.0F), far_plane(0.F)

{}

PointLight::PointLight(GLuint shadow_width,
  GLuint shadow_height,
  GLfloat near,
  GLfloat far,
  GLfloat red,
  GLfloat green,
  GLfloat blue,
  GLfloat radiance,
  GLfloat x_pos,
  GLfloat y_pos,
  GLfloat z_pos,
  GLfloat con,
  GLfloat lin,
  GLfloat exp)
  :

    Light(red, green, blue, radiance), omni_dir_shadow_map(std::make_shared<OmniDirShadowMap>()),

    position(glm::vec3(x_pos, y_pos, z_pos)), constant(con), linear(lin), exponent(exp), far_plane(far)
{
    float const aspect = static_cast<float>(shadow_width) / static_cast<float>(shadow_height);
    light_proj = glm::perspective(glm::radians(90.0F), aspect, near, far);
    omni_dir_shadow_map->init(shadow_width, shadow_height);
}

auto PointLight::calculate_light_transform() -> std::vector<glm::mat4>
{
    std::vector<glm::mat4> light_matrices;
    // make sure all light matrices align with the order we were defining in
    // OmniShadowMap GL_TEXTURE_CUBE_MAP_POSITIVE_X+i; therefoe start off with
    // glm::vec3(1.0, 0.0,0.0) +x,-x
    light_matrices.push_back(
      light_proj * glm::lookAt(position, position + glm::vec3(1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0)));
    light_matrices.push_back(
      light_proj * glm::lookAt(position, position + glm::vec3(-1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0)));

    //+y,-y
    light_matrices.push_back(
      light_proj * glm::lookAt(position, position + glm::vec3(0.0, 1.0, 0.0), glm::vec3(0.0, 0.0, 1.0)));
    light_matrices.push_back(
      light_proj * glm::lookAt(position, position + glm::vec3(0.0, -1.0, 0.0), glm::vec3(0.0, 0.0, -1.0)));

    //+z,-z
    light_matrices.push_back(
      light_proj * glm::lookAt(position, position + glm::vec3(0.0, 0.0, 1.0), glm::vec3(0.0, -1.0, 0.0)));
    light_matrices.push_back(
      light_proj * glm::lookAt(position, position + glm::vec3(0.0, 0.0, -1.0), glm::vec3(0.0, -1.0, 0.0)));

    return light_matrices;
}

void PointLight::set_position(glm::vec3 position) { this->position = position; }

auto PointLight::get_far_plane() const -> GLfloat { return far_plane; }

auto PointLight::get_position() -> glm::vec3 { return position; }

PointLight::~PointLight() = default;
