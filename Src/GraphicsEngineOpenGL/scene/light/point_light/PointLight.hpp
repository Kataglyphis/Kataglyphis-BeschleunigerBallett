#pragma once
#include <memory>
#include <vector>

#include "scene/light/Light.hpp"
#include "OmniDirShadowMap.hpp"

class PointLight : public Light {
 public:
  PointLight();

  PointLight(GLuint shadow_width, GLuint shadow_height, GLfloat near,
             GLfloat far, GLfloat red, GLfloat green, GLfloat blue,
             GLfloat radiance, GLfloat x_pos, GLfloat y_pos, GLfloat z_pos,
             GLfloat con, GLfloat lin, GLfloat exp);

  std::vector<glm::mat4> calculate_light_transform();

  void set_position(glm::vec3 position);

  std::shared_ptr<OmniDirShadowMap> get_omni_shadow_map() {
    return omni_dir_shadow_map;
  };
  GLfloat get_far_plane();
  glm::vec3 get_position();
  GLfloat get_constant_factor() { return constant; };
  GLfloat get_linear_factor() { return linear; };
  ;
  GLfloat get_exponent_factor() { return exponent; };
  ;

  ~PointLight();

 protected:
  std::shared_ptr<OmniDirShadowMap> omni_dir_shadow_map;

  glm::vec3 position;

  GLfloat constant, linear, exponent;

  GLfloat far_plane;
};
