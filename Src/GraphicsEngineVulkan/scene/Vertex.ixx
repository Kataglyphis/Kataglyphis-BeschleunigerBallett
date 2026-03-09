module;

#include "scene/Vertex.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <vulkan/vulkan.h>

#include <array>

export module kataglyphis.vulkan.vertex;

export using ::Vertex;

export namespace vertex {
std::array<VkVertexInputAttributeDescription, 4> getVertexInputAttributeDesc();
}
