module;

#include <array>
#include <cstddef>
#include <glm/glm.hpp>
#include <span>
#include <vector>
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

void fillMissingFlatNormals(std::span<Vertex> vertices, std::span<const unsigned int> indices, std::size_t firstIndex)
{
    for (std::size_t i = firstIndex; i + 2 < indices.size(); i += 3) {
        Vertex &v0 = vertices[indices[i + 0]];
        Vertex &v1 = vertices[indices[i + 1]];
        Vertex &v2 = vertices[indices[i + 2]];

        const glm::vec3 faceNormal = glm::cross(v1.position - v0.position, v2.position - v0.position);
        if (glm::dot(faceNormal, faceNormal) <= 0.0F) { continue; }
        const glm::vec3 n = glm::normalize(faceNormal);
        if (glm::dot(v0.normal, v0.normal) <= 1e-12F) { v0.normal = n; }
        if (glm::dot(v1.normal, v1.normal) <= 1e-12F) { v1.normal = n; }
        if (glm::dot(v2.normal, v2.normal) <= 1e-12F) { v2.normal = n; }
    }
}

auto getVertexInputAttributeDesc() -> std::array<vk::VertexInputAttributeDescription, 5>
{
    std::array<vk::VertexInputAttributeDescription, 5> attribute_describtions{};

    attribute_describtions[0] =
      vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position));

    attribute_describtions[1] =
      vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal));

    attribute_describtions[2] =
      vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, color));

    attribute_describtions[3] =
      vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texture_coords));

    attribute_describtions[4] =
      vk::VertexInputAttributeDescription(4, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, tangent));

    return attribute_describtions;
}

// Per-vertex tangent frame from triangle UV gradients (Lengyel's method),
// ported from the Rust renderer's compute_tangents
// (ExternalLib/Kataglyphis-RustProjectTemplate/.../gltf_loader.rs). Accumulates
// BOTH tangent and bitangent per vertex across every triangle in
// indices[firstIndex..], then Gram-Schmidt-orthogonalizes against the
// (already final) vertex normal and stores the handedness sign
// sign(dot(cross(N, T), accumulatedBitangent)) in .w - the glTF convention the
// shading pass will reconstruct the bitangent from. Not full MikkTSpace: a
// vertex shared by UV islands of opposite handedness gets one averaged frame
// rather than being split at the seam.
void computeTangents(std::span<Vertex> vertices, std::span<const unsigned int> indices, std::size_t firstIndex)
{
    std::vector<glm::vec3> tangentAccum(vertices.size(), glm::vec3(0.0F));
    std::vector<glm::vec3> bitangentAccum(vertices.size(), glm::vec3(0.0F));

    for (std::size_t i = firstIndex; i + 2 < indices.size(); i += 3) {
        const unsigned int corners[3] = { indices[i + 0], indices[i + 1], indices[i + 2] };
        const Vertex &v0 = vertices[corners[0]];
        const Vertex &v1 = vertices[corners[1]];
        const Vertex &v2 = vertices[corners[2]];

        const glm::vec3 e1 = v1.position - v0.position;
        const glm::vec3 e2 = v2.position - v0.position;
        const glm::vec2 d1 = v1.texture_coords - v0.texture_coords;
        const glm::vec2 d2 = v2.texture_coords - v0.texture_coords;

        const float det = (d1.x * d2.y) - (d2.x * d1.y);
        // Degenerate UV mapping (zero-area UV triangle): contribute nothing
        // rather than divide by (near-)zero, which would seed the accumulator
        // with NaN/Inf that survives normalization below.
        if (glm::abs(det) < 1e-8F) { continue; }
        const float invDet = 1.0F / det;
        const glm::vec3 tangent = ((e1 * d2.y) - (e2 * d1.y)) * invDet;
        const glm::vec3 bitangent = ((e2 * d1.x) - (e1 * d2.x)) * invDet;

        for (unsigned int corner : corners) {
            tangentAccum[corner] += tangent;
            bitangentAccum[corner] += bitangent;
        }
    }

    for (std::size_t i = firstIndex; i + 2 < indices.size(); i += 3) {
        for (unsigned int corner : { indices[i + 0], indices[i + 1], indices[i + 2] }) {
            Vertex &v = vertices[corner];
            const glm::vec3 n = v.normal;

            glm::vec3 t = tangentAccum[corner] - (n * glm::dot(n, tangentAccum[corner]));
            if (glm::dot(t, t) > 1e-12F) {
                t = glm::normalize(t);
            } else {
                // Degenerate UVs: nothing survived Gram-Schmidt. Fall back to
                // an arbitrary axis orthogonal to the normal rather than
                // produce a NaN.
                glm::vec3 fallback = glm::cross(n, glm::vec3(0.0F, 1.0F, 0.0F));
                if (glm::dot(fallback, fallback) <= 1e-12F) { fallback = glm::cross(n, glm::vec3(1.0F, 0.0F, 0.0F)); }
                t = glm::normalize(fallback);
            }

            const float w = glm::dot(glm::cross(n, t), bitangentAccum[corner]) < 0.0F ? -1.0F : 1.0F;
            v.tangent = glm::vec4(t, w);
        }
    }
}

}// namespace vertex