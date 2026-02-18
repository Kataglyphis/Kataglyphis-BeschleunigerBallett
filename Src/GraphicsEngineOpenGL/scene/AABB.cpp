#include "scene/AABB.hpp"
#include "scene/Mesh.hpp"

#include <vector>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glad/glad.h>
#include <glm/ext/vector_float2.hpp>
#include <memory>
#include <cstdlib>

AABB::AABB() = default;

auto AABB::get_corners(glm::mat4 model) -> std::vector<glm::vec3>
{
    std::vector<glm::vec3> corners_world_space;

    corners_world_space.reserve(corners.size());
    for (glm::vec3 const corner : corners) { corners_world_space.emplace_back(model * glm::vec4(corner, 1.0F)); }

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

    corners.emplace_back(minX, minY, minZ);
    corners.emplace_back(minX, minY, maxZ);
    corners.emplace_back(minX, maxY, minZ);
    corners.emplace_back(minX, maxY, maxZ);
    corners.emplace_back(maxX, minY, minZ);
    corners.emplace_back(maxX, minY, maxZ);
    corners.emplace_back(maxX, maxY, minZ);
    corners.emplace_back(maxX, maxY, maxZ);

    // 0: left  bottom  front
    vertices.emplace_back(glm::vec3(minX, minY, maxZ), glm::vec3(0.F), glm::vec3(0.F), glm::vec2(0.F));
    // 1: right bottom  front
    vertices.emplace_back(glm::vec3(maxX, minY, maxZ), glm::vec3(0.F), glm::vec3(0.F), glm::vec2(0.F));
    // 2: left  top     front
    vertices.emplace_back(glm::vec3(minX, maxY, maxZ), glm::vec3(0.F), glm::vec3(0.F), glm::vec2(0.F));
    // 3: right top     front
    vertices.emplace_back(glm::vec3(maxX, maxY, maxZ), glm::vec3(0.F), glm::vec3(0.F), glm::vec2(0.F));
    // 4: left  bottom  far
    vertices.emplace_back(glm::vec3(minX, minY, minZ), glm::vec3(0.F), glm::vec3(0.F), glm::vec2(0.F));
    // 5: right bottom  far
    vertices.emplace_back(glm::vec3(maxX, minY, minZ), glm::vec3(0.F), glm::vec3(0.F), glm::vec2(0.F));
    // 6: left  top     far
    vertices.emplace_back(glm::vec3(minX, maxY, minZ), glm::vec3(0.F), glm::vec3(0.F), glm::vec2(0.F));
    // 7: right top     far
    vertices.emplace_back(glm::vec3(maxX, maxY, minZ), glm::vec3(0.F), glm::vec3(0.F), glm::vec2(0.F));

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
