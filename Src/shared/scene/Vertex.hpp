#ifndef KATAGLYPHIS_SHARED_SCENE_VERTEX_HPP
#define KATAGLYPHIS_SHARED_SCENE_VERTEX_HPP

#ifdef __cplusplus
#include <glm/glm.hpp>
#define KTG_VEC2 glm::vec2
#define KTG_VEC3 glm::vec3
#define GLM_ENABLE_EXPERIMENTAL
#include <functional>
#include <glm/gtx/hash.hpp>
#else
#define KTG_VEC2 vec2
#define KTG_VEC3 vec3
#endif

struct Vertex
{
    KTG_VEC3 position;
    KTG_VEC3 normal;
    KTG_VEC3 color;
    KTG_VEC2 texture_coords;

#ifdef __cplusplus
    Vertex() = default;

    Vertex(glm::vec3 pos, glm::vec3 normal, glm::vec3 color, glm::vec2 texture_coords)
      : position(pos), normal(normal), color(color), texture_coords(texture_coords)
    {}

    glm::vec3 get_position() const { return position; }
    glm::vec3 get_normal() const { return normal; }
    glm::vec3 get_color() const { return color; }
    glm::vec2 get_tex_coors() const { return texture_coords; }

    bool operator==(const Vertex &other) const
    {
        return position == other.position && normal == other.normal && texture_coords == other.texture_coords
               && color == other.color;
    }
#endif
};

#ifdef __cplusplus
namespace std {

template<> struct hash<Vertex>
{
    size_t operator()(Vertex const &vertex) const
    {
        size_t const hash_position = hash<glm::vec3>()(vertex.position);
        size_t const hash_normal = hash<glm::vec3>()(vertex.normal);
        size_t const hash_texture = hash<glm::vec2>()(vertex.texture_coords);

        return hash_position ^ (hash_normal << 1U) ^ (hash_texture << 2U);
    }
};

}// namespace std
#endif

#undef KTG_VEC2
#undef KTG_VEC3

#endif
