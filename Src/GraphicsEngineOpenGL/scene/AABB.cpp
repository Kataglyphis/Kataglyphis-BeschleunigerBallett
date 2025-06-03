#include "scene/AABB.hpp"

#include <algorithm>

AABB::AABB() {}

std::vector<glm::vec3> AABB::get_corners(glm::mat4 model)
{
    std::vector<glm::vec3> corners_world_space;

    for (glm::vec3 corner : corners) { corners_world_space.push_back(glm::vec3(model * glm::vec4(corner, 1.0f))); }

    return corners_world_space;
}

void AABB::init(GLfloat minX, GLfloat maxX, GLfloat minY, GLfloat maxY, GLfloat minZ, GLfloat maxZ)
{
    this->minX = minX;
    this->maxX = maxX;
    this->minY = minY;
    this->maxY = maxY;
    this->minZ = minZ;
    this->maxZ = maxZ;

    corners.push_back(glm::vec3(minX, minY, minZ));
    corners.push_back(glm::vec3(minX, minY, maxZ));
    corners.push_back(glm::vec3(minX, maxY, minZ));
    corners.push_back(glm::vec3(minX, maxY, maxZ));
    corners.push_back(glm::vec3(maxX, minY, minZ));
    corners.push_back(glm::vec3(maxX, minY, maxZ));
    corners.push_back(glm::vec3(maxX, maxY, minZ));
    corners.push_back(glm::vec3(maxX, maxY, maxZ));

    // 0: left  bottom  front
    vertices.push_back(Vertex(glm::vec3(minX, minY, maxZ), glm::vec3(0.f), glm::vec3(0.f), glm::vec2(0.f)));
    // 1: right bottom  front
    vertices.push_back(Vertex(glm::vec3(maxX, minY, maxZ), glm::vec3(0.f), glm::vec3(0.f), glm::vec2(0.f)));
    // 2: left  top     front
    vertices.push_back(Vertex(glm::vec3(minX, maxY, maxZ), glm::vec3(0.f), glm::vec3(0.f), glm::vec2(0.f)));
    // 3: right top     front
    vertices.push_back(Vertex(glm::vec3(maxX, maxY, maxZ), glm::vec3(0.f), glm::vec3(0.f), glm::vec2(0.f)));
    // 4: left  bottom  far
    vertices.push_back(Vertex(glm::vec3(minX, minY, minZ), glm::vec3(0.f), glm::vec3(0.f), glm::vec2(0.f)));
    // 5: right bottom  far
    vertices.push_back(Vertex(glm::vec3(maxX, minY, minZ), glm::vec3(0.f), glm::vec3(0.f), glm::vec2(0.f)));
    // 6: left  top     far
    vertices.push_back(Vertex(glm::vec3(minX, maxY, minZ), glm::vec3(0.f), glm::vec3(0.f), glm::vec2(0.f)));
    // 7: right top     far
    vertices.push_back(Vertex(glm::vec3(maxX, maxY, minZ), glm::vec3(0.f), glm::vec3(0.f), glm::vec2(0.f)));

    indices = { // note that we start from 0!

        // left
        4,
        2,
        6,
        4,
        0,
        2,

        // right
        3,
        5,
        7,
        5,
        3,
        1,

        // top
        2,
        3,
        6,
        6,
        3,
        7,

        // bottom
        4,
        1,
        0,
        5,
        1,
        4,

        // back
        7,
        4,
        6,
        5,
        4,
        7,

        // front
        0,
        3,
        2,
        0,
        1,
        3

    };

    mesh = std::make_shared<Mesh>(vertices, indices);
}

glm::vec3 AABB::get_radius() { return glm::vec3(std::abs(maxX - minX), std::abs(maxY - minY), std::abs(maxZ - minZ)); }

void AABB::render() { mesh->render(); }

AABB::~AABB() {}
