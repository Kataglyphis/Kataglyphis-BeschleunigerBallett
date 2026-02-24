module;

#include "scene/Vertex.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

export module kataglyphis.vulkan.vertex;

export using ::Vertex;

export namespace vertex {
std::array<VkVertexInputAttributeDescription, 4> getVertexInputAttributeDesc();
}

export namespace std {
template<> struct hash<Vertex>
{
    size_t operator()(Vertex const &vertex) const
    {
        size_t h1 = hash<glm::vec3>()(vertex.pos);
        size_t h2 = hash<glm::vec3>()(vertex.color);
        size_t h3 = hash<glm::vec2>()(vertex.texture_coords);
        size_t h4 = hash<glm::vec3>()(vertex.normal);

        return (((((((h2 << 1) ^ h1) >> 1) ^ h3) << 1) ^ h4));
    }
};
}// namespace std
