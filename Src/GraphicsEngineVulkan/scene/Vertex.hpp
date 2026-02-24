#pragma once

#ifdef __cplusplus
#include <glm/glm.hpp>
#define KTG_VEC2 glm::vec2
#define KTG_VEC3 glm::vec3
#else
#define KTG_VEC2 vec2
#define KTG_VEC3 vec3
#endif

struct Vertex
{
    KTG_VEC3 pos;
    KTG_VEC3 normal;
    KTG_VEC3 color;
    KTG_VEC2 texture_coords;

#ifdef __cplusplus
    bool operator==(const Vertex &other) const
    {
        return pos == other.pos && normal == other.normal && texture_coords == other.texture_coords;
    }
#endif
};

#undef KTG_VEC2
#undef KTG_VEC3
