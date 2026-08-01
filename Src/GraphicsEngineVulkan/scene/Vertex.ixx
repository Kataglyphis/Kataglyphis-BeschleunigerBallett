module;

#include "scene/Vertex.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <vulkan/vulkan.hpp>

#include <array>
#include <cstddef>
#include <span>

export module kataglyphis.vulkan.vertex;

export using ::Vertex;

export namespace vertex {
std::array<vk::VertexInputAttributeDescription, 4> getVertexInputAttributeDesc();

/// Per-triangle geometric (flat) normal, assigned to all three vertices of
/// every triangle in indices[firstIndex..]. The OBJ and glTF loaders each
/// hand-rolled this loop; this is the only copy left, so a third loader
/// adding its own is the bug this function exists to prevent. Bounded by
/// `i + 2 < indices.size()` and skips zero-area (degenerate) triangles,
/// which would otherwise normalize a zero vector into NaN.
void computeFlatNormals(std::span<Vertex> vertices, std::span<const unsigned int> indices, std::size_t firstIndex = 0);
}
