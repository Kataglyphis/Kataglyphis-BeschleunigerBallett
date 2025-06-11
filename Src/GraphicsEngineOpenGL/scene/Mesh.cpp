#include "scene/Mesh.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

Mesh::Mesh() : m_vao(-1), m_ibo(-1), m_drawCount(0), vertices(std::vector<Vertex>()), indices(std::vector<uint32_t>())
{}

Mesh::Mesh(std::vector<Vertex> &vertices, std::vector<unsigned int> &indices)
  :

    vertices(vertices), indices(indices)
{
    uint32_t numVertices = static_cast<uint32_t>(vertices.size());
    uint32_t num_indices = static_cast<uint32_t>(indices.size());

    m_drawCount = num_indices;
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    // Dynamic Draw = lower Performence.
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(this->indices[0]) * num_indices, &(this->indices[0]), GL_DYNAMIC_DRAW);

    glGenBuffers(NUM_BUFFERS, m_vab);
    glBindBuffer(GL_ARRAY_BUFFER, m_vab[POSITION_VB]);
    glBufferData(GL_ARRAY_BUFFER, numVertices * sizeof(this->vertices[0]), &(this->vertices[0]), GL_DYNAMIC_DRAW);

    // enable Vertex Atrribs for Pos, Norm, Textcood
    //  Vertex Position
    // Stride bytes: just the size of Vertex, offset = use offsetof funktion
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, position));
    glEnableVertexAttribArray(0);
    // Vertex Normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, normal));
    glEnableVertexAttribArray(1);
    // Vertex Normal
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, color));
    glEnableVertexAttribArray(2);
    // Vertex Texture Cood
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)(offsetof(Vertex, texture_coords)));
    glEnableVertexAttribArray(3);

    // unbind everything after setting the attribs
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void Mesh::render()
{
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    // Draw Triangles
    glDrawElements(GL_TRIANGLES, m_drawCount, GL_UNSIGNED_INT, 0);

    // unbind all again
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

Mesh::~Mesh()
{
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_ibo);
    glDeleteBuffers(NUM_BUFFERS, m_vab);
}
