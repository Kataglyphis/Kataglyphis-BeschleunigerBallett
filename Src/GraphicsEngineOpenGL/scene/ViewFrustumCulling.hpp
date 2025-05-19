#pragma once

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <vector>

#include "scene/AABB.hpp"
#include "camera/Camera.hpp"
#include "scene/texture/Texture.hpp"
#include "hostDevice/GlobalValues.hpp"

class ViewFrustumCulling {
 public:
  ViewFrustumCulling();

  bool is_inside(GLfloat ratio, std::shared_ptr<Camera> main_camera,
                 std::shared_ptr<AABB> bounding_box, glm::mat4 model);

  void render_view_frustum();

  ~ViewFrustumCulling();

 private:
  // render view frustum
  unsigned int VBO, VAO, EBO;

  unsigned int m_drawCount;

  // we get that as input
  GLfloat near_plane, far_plane, fov, ratio;

  // calculate as soon as we become params
  GLfloat tan, near_height, near_width, far_height, far_width;

  std::shared_ptr<Camera> main_camera;

  glm::vec3 dir, near_center, far_center;

  // all corners of the frustum
  // near plane
  glm::vec3 near_top_left, near_top_right, near_bottom_left, near_bottom_right;
  // far plane
  glm::vec3 far_top_left, far_top_right, far_bottom_left, far_bottom_right;

  // planes in Hesse normal form
  // layout: [0]: near plane, [1] far plane, [2] front, [3] bottom, [4]: left,
  // [5]: right
  struct frustum_plane {
    glm::vec3 normal;
    glm::vec3 position;
  };

  frustum_plane frustum_planes[NUM_FRUSTUM_PLANES];

  void init(std::vector<glm::vec3> frustum_corner);

  bool corners_outside_plane(std::vector<glm::vec3> aabb_corners,
                             frustum_plane plane, GLuint outcode_pattern);

  GLfloat plane_point_distance(frustum_plane plane, glm::vec3 corner);

  void update_frustum_param(GLfloat near_plane, GLfloat far_plane, GLfloat fov,
                            GLfloat ratio, std::shared_ptr<Camera> main_camera);
};
