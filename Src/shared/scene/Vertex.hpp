#ifndef KATAGLYPHIS_SHARED_SCENE_VERTEX_HPP
#define KATAGLYPHIS_SHARED_SCENE_VERTEX_HPP

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <functional>
#include <glm/gtx/hash.hpp>

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 texture_coords;

    Vertex() = default;

    Vertex(glm::vec3 pos, glm::vec3 normal, glm::vec3 color, glm::vec2 texture_coords)
      : position(pos), normal(normal), color(color), texture_coords(texture_coords)
    {}

    glm::vec3 get_position() const { return position; }

    bool operator==(const Vertex &other) const
    {
        return position == other.position && normal == other.normal && texture_coords == other.texture_coords
               && color == other.color;
    }
};

namespace std {

template<> struct hash<Vertex>
{
    size_t operator()(Vertex const &vertex) const
    {
        // XOR with 1- and 2-bit shifts barely mixes: two vertices differing
        // only in normal land in nearby buckets, and clustered buckets turn
        // hash lookups into linear scans. This is the usual 64-bit combine
        // (boost's, with a 64-bit constant), which spreads each component
        // across the whole word before combining.
        auto combine = [](size_t seed, size_t value) {
            return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
        };

        size_t seed = hash<glm::vec3>()(vertex.position);
        seed = combine(seed, hash<glm::vec3>()(vertex.normal));
        seed = combine(seed, hash<glm::vec2>()(vertex.texture_coords));
        // color participates in operator==, so it must participate here too -
        // omitting it is legal but makes every colour variant of a position
        // collide.
        seed = combine(seed, hash<glm::vec3>()(vertex.color));
        return seed;
    }
};

}// namespace std

#endif
