#include "scene/Vertex.hpp"
#include <array>
#include <cstddef>
#include <vulkan/vulkan_core.h>

Vertex::Vertex()
{
    this->pos = glm::vec3(-1.F);
    this->normal = glm::vec3(-1.F);
    this->color = glm::vec3(-1.F);
    this->texture_coords = glm::vec3(-1.F);
}

Vertex::Vertex(glm::vec3 pos, glm::vec3 normal, glm::vec3 color, glm::vec2 texture_coords)
  : pos(pos), normal(normal), color(color), texture_coords(texture_coords)
{}

namespace vertex {

auto getVertexInputAttributeDesc() -> std::array<VkVertexInputAttributeDescription, 4>
{
    std::array<VkVertexInputAttributeDescription, 4> attribute_describtions{};

    // Position attribute
    attribute_describtions[0].binding = 0;
    attribute_describtions[0].location = 0;
    attribute_describtions[0].format = VK_FORMAT_R32G32B32_SFLOAT;// format data will take (also helps define
                                                                  // size of data)
    attribute_describtions[0].offset = offsetof(Vertex, pos);

    // normal coord attribute
    attribute_describtions[1].binding = 0;
    attribute_describtions[1].location = 1;
    attribute_describtions[1].format = VK_FORMAT_R32G32B32_SFLOAT;// format data will take (also helps define
                                                                  // size of data)
    attribute_describtions[1].offset = offsetof(Vertex, normal);// where this attribute is defined in the data
                                                                // for a single vertex

    // normal coord attribute
    attribute_describtions[2].binding = 0;
    attribute_describtions[2].location = 2;
    attribute_describtions[2].format = VK_FORMAT_R32G32B32_SFLOAT;// format data will take (also helps define
                                                                  // size of data)
    attribute_describtions[2].offset = offsetof(Vertex, color);

    attribute_describtions[3].binding = 0;
    // texture coord attribute
    attribute_describtions[3].location = 3;
    attribute_describtions[3].format = VK_FORMAT_R32G32_SFLOAT;// format data will take (also helps define size
                                                               // of data)
    attribute_describtions[3].offset = offsetof(Vertex, texture_coords);// where this attribute is defined in
                                                                        // the data for a single vertex

    return attribute_describtions;
}

}// namespace vertex
