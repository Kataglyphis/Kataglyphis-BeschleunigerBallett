module;

#include "scene/Vertex.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <vulkan/vulkan.hpp>

#include <array>

export module kataglyphis.vulkan.vertex;

export using ::Vertex;

export namespace vertex {
std::array<vk::VertexInputAttributeDescription, 4> getVertexInputAttributeDesc();
}
