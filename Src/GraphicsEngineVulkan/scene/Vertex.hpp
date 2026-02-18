// this little "hack" is needed for using it on the
// CPU side as well for the GPU side :)
// inspired by the NVDIDIA tutorial:
// https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/
#ifdef __cplusplus
#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <vulkan/vulkan.h>

#include <array>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <vector>

class Vertex
{
  public:
    Vertex();
    Vertex(glm::vec3 pos, glm::vec3 normal, glm::vec3 color, glm::vec2 texture_coords);

    glm::vec3 pos{};
    glm::vec3 normal{};
    glm::vec3 color{};
    glm::vec2 texture_coords{};

    bool operator==(const Vertex &other) const
    {
        return pos == other.pos && normal == other.normal && texture_coords == other.texture_coords;
    }
};

namespace vertex {

std::array<VkVertexInputAttributeDescription, 4> getVertexInputAttributeDesc();

}

namespace std {
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
#else
struct Vertex
{
    vec3 pos;
    vec3 normal;
    vec3 color;
    vec2 texture_coords;
};

#endif
