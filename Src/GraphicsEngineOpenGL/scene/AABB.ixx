module;

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <vector>

export module kataglyphis.opengl.aabb;

import kataglyphis.opengl.mesh;
import kataglyphis.opengl.vertex;

export class AABB
{
  public:
    AABB();

    std::vector<glm::vec3> get_corners(glm::mat4 model);

    void init(GLfloat minX, GLfloat maxX, GLfloat minY, GLfloat maxY, GLfloat minZ, GLfloat maxZ);

    glm::vec3 get_radius() const;

    void render();

    ~AABB();

  private:
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    std::shared_ptr<Mesh> mesh;

    std::vector<glm::vec3> corners;

    GLfloat minX{}, maxX{}, minY{}, maxY{}, minZ{}, maxZ{};
};
