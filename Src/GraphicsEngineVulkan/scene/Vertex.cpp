module;

#include <array>
#include <cstddef>
#include <glm/glm.hpp>
#include <span>
#include <vulkan/vulkan.hpp>

module kataglyphis.vulkan.vertex;

namespace vertex {

// Indices are trusted to already address existing vertices - that
// validation is the caller's contract (ObjLoader.cpp's face_valid guard,
// GltfLoader.cpp's emitTri guard), not this function's. An out-of-range
// index here is an unchecked std::span subscript: an out-of-bounds write.
void computeFlatNormals(std::span<Vertex> vertices, std::span<const unsigned int> indices, std::size_t firstIndex)
{
    for (std::size_t i = firstIndex; i + 2 < indices.size(); i += 3) {
        Vertex &v0 = vertices[indices[i + 0]];
        Vertex &v1 = vertices[indices[i + 1]];
        Vertex &v2 = vertices[indices[i + 2]];

        const glm::vec3 faceNormal = glm::cross(v1.position - v0.position, v2.position - v0.position);
        // Degenerate triangle (zero area): leave the existing normal rather
        // than emit a NaN from normalizing a zero vector.
        if (glm::dot(faceNormal, faceNormal) <= 0.0F) { continue; }
        const glm::vec3 n = glm::normalize(faceNormal);
        v0.normal = n;
        v1.normal = n;
        v2.normal = n;
    }
}

auto getVertexInputAttributeDesc() -> std::array<vk::VertexInputAttributeDescription, 4>
{
    std::array<vk::VertexInputAttributeDescription, 4> attribute_describtions{};

    attribute_describtions[0] =
      vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position));

    attribute_describtions[1] =
      vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal));

    attribute_describtions[2] =
      vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color));

    attribute_describtions[3] =
      vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texture_coords));

    return attribute_describtions;
}

}// namespace vertex