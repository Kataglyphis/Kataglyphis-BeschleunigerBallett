module;

#include <cstdlib>
#include <memory>
#include <vector>

#include <glad/glad.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

module kataglyphis.opengl.aabb;

import kataglyphis.opengl.mesh;

AABB::AABB() = default;

auto AABB::get_corners(glm::mat4 model) -> std::vector<glm::vec3>
{
    std::vector<glm::vec3> corners_world_space;

    corners_world_space.reserve(corners.size());
    for (glm::vec3 const corner : corners) { corners_world_space.emplace_back(model * glm::vec4(corner, 1.0F)); }

    return corners_world_space;
}

void AABB::init(GLfloat min_x, GLfloat max_x, GLfloat min_y, GLfloat max_y, GLfloat min_z, GLfloat max_z)
{
    this->minX = min_x;
    this->maxX = max_x;
    this->minY = min_y;
    this->maxY = max_y;
    this->minZ = min_z;
    this->maxZ = max_z;

    corners.emplace_back(min_x, min_y, min_z);
    corners.emplace_back(min_x, min_y, max_z);
    corners.emplace_back(min_x, max_y, min_z);
    corners.emplace_back(min_x, max_y, max_z);
    corners.emplace_back(max_x, min_y, min_z);
    corners.emplace_back(max_x, min_y, max_z);
    corners.emplace_back(max_x, max_y, min_z);
    corners.emplace_back(max_x, max_y, max_z);

    // 0: left  bottom  front
    vertices.emplace_back(glm::vec3(min_x, min_y, max_z), glm::vec3(0.F), glm::vec3(0.F), glm::vec2(0.F));
    // 1: right bottom  front
    vertices.emplace_back(glm::vec3(max_x, min_y, max_z), glm::vec3(0.F), glm::vec3(0.F), glm::vec2(0.F));
    // 2: left  top     front
    vertices.emplace_back(glm::vec3(min_x, max_y, max_z), glm::vec3(0.F), glm::vec3(0.F), glm::vec2(0.F));
    // 3: right top     front
    vertices.emplace_back(glm::vec3(max_x, max_y, max_z), glm::vec3(0.F), glm::vec3(0.F), glm::vec2(0.F));
    // 4: left  bottom  far
    vertices.emplace_back(glm::vec3(min_x, min_y, min_z), glm::vec3(0.F), glm::vec3(0.F), glm::vec2(0.F));
    // 5: right bottom  far
    vertices.emplace_back(glm::vec3(max_x, min_y, min_z), glm::vec3(0.F), glm::vec3(0.F), glm::vec2(0.F));
    // 6: left  top     far
    vertices.emplace_back(glm::vec3(min_x, max_y, min_z), glm::vec3(0.F), glm::vec3(0.F), glm::vec2(0.F));
    // 7: right top     far
    vertices.emplace_back(glm::vec3(max_x, max_y, min_z), glm::vec3(0.F), glm::vec3(0.F), glm::vec2(0.F));

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

auto AABB::get_radius() const -> glm::vec3
{
    return { std::abs(maxX - minX), std::abs(maxY - minY), std::abs(maxZ - minZ) };
}

void AABB::render() { mesh->render(); }

AABB::~AABB() = default;
