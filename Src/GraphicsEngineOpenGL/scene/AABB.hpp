#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <vector>

#include "scene/Mesh.hpp"

class AABB {
 public:
  AABB();

  std::vector<glm::vec3> get_corners(glm::mat4 model);

  void init(GLfloat minX, GLfloat maxX, GLfloat minY, GLfloat maxY,
            GLfloat minZ, GLfloat maxZ);

  glm::vec3 get_radius();

  void render();

  ~AABB();

 private:
  std::vector<Vertex> vertices;
  std::vector<unsigned int> indices;

  std::shared_ptr<Mesh> mesh;

  std::vector<glm::vec3> corners;

  GLfloat minX, maxX, minY, maxY, minZ, maxZ;
};
