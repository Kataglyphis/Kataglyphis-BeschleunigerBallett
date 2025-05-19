#pragma once
#include <array>
#include <limits>
#include <memory>
#include <vector>

#include "scene/light/Light.hpp"
#include "hostDevice/host_device_shared.hpp"
#include "scene/light/directional_light/CascadedShadowMap.hpp"

class DirectionalLight : public Light {
 public:
  DirectionalLight();

  DirectionalLight(GLuint shadow_width, GLuint shadow_height, GLfloat red,
                   GLfloat green, GLfloat blue, GLfloat radiance, GLfloat x_dir,
                   GLfloat y_dir, GLfloat z_dir, GLfloat near_plane,
                   GLfloat far_plane, int num_cascades);

  glm::mat4 calculate_light_transform();

  std::shared_ptr<CascadedShadowMap> get_shadow_map() const {
    return shadow_map;
  }

  glm::vec3 get_direction() const;
  glm::vec3 get_color() const;
  float get_radiance() const;
  glm::mat4 get_light_view_matrix() const;
  std::vector<GLfloat> get_cascaded_slots() const;
  std::vector<glm::mat4>& get_cascaded_light_matrices();

  void set_direction(glm::vec3 direction);
  void set_radiance(float radiance);
  void set_color(glm::vec3 color);

  void update_shadow_map(GLfloat shadow_width, GLfloat shadow_height,
                         GLuint num_cascades);

  void calc_orthogonal_projections(glm::mat4 camera_view_matrix, GLfloat fov,
                                   GLuint window_width, GLuint window_height,
                                   GLuint current_num_cascades);

  ~DirectionalLight();

 private:
  std::vector<glm::vec4> get_frustum_corners_world_space(const glm::mat4& proj,
                                                         const glm::mat4& view);
  void calc_cascaded_slots();

  std::shared_ptr<CascadedShadowMap> shadow_map;

  glm::vec3 direction;
  GLfloat shadow_near_plane, shadow_far_plane;

  std::array<GLfloat, NUM_CASCADES + 1> cascade_slots;
  std::vector<glm::mat4> cascade_light_matrices;
};
