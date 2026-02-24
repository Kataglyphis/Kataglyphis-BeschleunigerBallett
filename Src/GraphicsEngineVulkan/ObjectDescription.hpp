#pragma once

#ifdef __cplusplus
#include <cstdint>
#endif

struct ObjectDescription
{
    uint64_t vertex_address;
    uint64_t index_address;
    uint64_t material_index_address;
    uint64_t material_address;
};
