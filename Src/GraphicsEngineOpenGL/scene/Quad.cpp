#include "scene/Quad.hpp"

#include <glm/glm.hpp>
#include <vector>

#include "hostDevice/GlobalValues.hpp"

Quad::Quad()
{
    glGenVertexArrays(1, &q_vao);
    glGenBuffers(1, &q_vbo);

    glBindVertexArray(q_vao);
    glBindBuffer(GL_ARRAY_BUFFER, q_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));
}

void Quad::render()
{
    glBindVertexArray(q_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

Quad::~Quad()
{
    glDeleteVertexArrays(1, &q_vao);
    glDeleteBuffers(1, &q_vbo);
}
