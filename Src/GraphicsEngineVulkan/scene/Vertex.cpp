module;

#include <array>
#include <cstddef>
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

module kataglyphis.vulkan.vertex;

namespace vertex {

auto getVertexInputAttributeDesc() -> std::array<vk::VertexInputAttributeDescription, 4>
{
    std::array<vk::VertexInputAttributeDescription, 4> attribute_describtions{};

    attribute_describtions[0] =
      vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position));

    attribute_describtions[1] =
      vk::VertexInputAttributeDescription(0, 1, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal));

    attribute_describtions[2] =
      vk::VertexInputAttributeDescription(0, 2, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color));

    attribute_describtions[3] =
      vk::VertexInputAttributeDescription(0, 3, vk::Format::eR32G32Sfloat, offsetof(Vertex, texture_coords));

    return attribute_describtions;
}

}// namespace vertex