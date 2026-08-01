module;

#include "ObjectDescription.hpp"
#include <algorithm>
#include <cstdint>
#include <span>

export module kataglyphis.vulkan.object_description;

export using ::ObjectDescription;

export namespace Kataglyphis {

/// Stamps texture_offset onto `descriptions`, which holds one entry per MESH
/// flattened across models (see Scene::add_model). Every mesh of a model
/// shares that model's offset into the flattened global texture array; the
/// offset advances by that model's texture count once its meshes are done.
/// Stops cleanly (leaving any trailing descriptions untouched) if the
/// per-model mesh counts would run past the end of `descriptions` - a
/// mesh-count/description-count mismatch must not read out of range.
inline void assignTextureOffsets(std::span<ObjectDescription> descriptions,
  std::span<const uint32_t> meshCountPerModel,
  std::span<const uint32_t> textureCountPerModel)
{
    const std::size_t modelCount = std::min(meshCountPerModel.size(), textureCountPerModel.size());
    std::size_t descIndex = 0;
    uint64_t offset = 0;
    for (std::size_t m = 0; m < modelCount; ++m) {
        for (uint32_t k = 0; k < meshCountPerModel[m]; ++k) {
            if (descIndex >= descriptions.size()) { return; }
            descriptions[descIndex].texture_offset = offset;
            ++descIndex;
        }
        offset += textureCountPerModel[m];
    }
}

}// namespace Kataglyphis
