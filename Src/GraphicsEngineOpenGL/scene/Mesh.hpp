#pragma once
#include <glad/glad.h>

#include <glm/glm.hpp>
#include <vector>

#include "hostDevice/GlobalValues.hpp"
#include "scene/Vertex.hpp"

// this a simple Mesh without mesh generation
class Mesh
{
  public:
    Mesh(std::vector<Vertex> &vertices, std::vector<unsigned int> &indices);

    Mesh();

    void render();

    std::vector<Vertex> getVertices() const { return this->vertices; }
    std::vector<unsigned int> getIndices() const { return this->indices; }

    ~Mesh();

  private:
    //  render data
    // unsigned int VAO, VBO, EBO;
    enum {
        POSITION = 0,
        NORMAL = 1,
        COLOR = 2,
        TEXTURECOORD = 3

    };

    enum { POSITION_VB, NUM_BUFFERS };

    // Vertex Array Object
    GLuint m_vao, m_ibo;
    // Vertex array buffer
    GLuint m_vab[NUM_BUFFERS];

    uint32_t m_drawCount;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
