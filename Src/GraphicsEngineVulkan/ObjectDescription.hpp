#ifndef KATAGLYPHIS_VULKAN_OBJECT_DESCRIPTION_HPP
#define KATAGLYPHIS_VULKAN_OBJECT_DESCRIPTION_HPP

#include <cstdint>

struct ObjectDescription
{
    uint64_t vertex_address;
    uint64_t index_address;
    uint64_t material_index_address;
    uint64_t material_address;
    // First slot of this model's textures in the flattened global texture
    // array. Material textureIDs are model-LOCAL; before this offset existed,
    // only model 0's textures were bound and every other model's IDs collided
    // with them. uint64 for layout parity with the fields above (scalar
    // layout, host header whose layout is pinned against
    // common/scene_types.slang's redeclaration).
    uint64_t texture_offset;
};

#endif
