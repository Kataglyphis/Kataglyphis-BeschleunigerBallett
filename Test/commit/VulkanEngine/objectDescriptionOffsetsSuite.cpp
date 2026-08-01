// assignTextureOffsets stamps texture_offset onto object_descriptions, which
// holds one entry per MESH flattened across models (Scene::add_model). The
// old create_object_description_buffer loop was bounded by getModelCount()
// and indexed descriptions[modelIndex], silently conflating "one description
// per mesh" with "one description per model" - a multi-mesh model followed by
// a second model shifted every offset by one slot. These pin the fix on the
// CPU, no device required.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

import kataglyphis.vulkan.object_description;

using Kataglyphis::assignTextureOffsets;

TEST(ObjectDescriptionOffsets, EveryMeshOfAModelSharesThatModelsOffset)
{
    // model 0: 3 meshes, 5 textures. model 1: 1 mesh, 2 textures.
    // descriptions are [m0.mesh0, m0.mesh1, m0.mesh2, m1.mesh0].
    std::vector<ObjectDescription> descriptions(4);
    const std::vector<uint32_t> meshCountPerModel{ 3, 1 };
    const std::vector<uint32_t> textureCountPerModel{ 5, 2 };

    assignTextureOffsets(descriptions, meshCountPerModel, textureCountPerModel);

    EXPECT_EQ(descriptions[0].texture_offset, 0U);
    EXPECT_EQ(descriptions[1].texture_offset, 0U);
    EXPECT_EQ(descriptions[2].texture_offset, 0U);
    EXPECT_EQ(descriptions[3].texture_offset, 5U);
}

TEST(ObjectDescriptionOffsets, ASingleModelLeavesEveryOffsetAtZero)
{
    std::vector<ObjectDescription> descriptions(2);
    const std::vector<uint32_t> meshCountPerModel{ 2 };
    const std::vector<uint32_t> textureCountPerModel{ 4 };

    assignTextureOffsets(descriptions, meshCountPerModel, textureCountPerModel);

    EXPECT_EQ(descriptions[0].texture_offset, 0U);
    EXPECT_EQ(descriptions[1].texture_offset, 0U);
}

TEST(ObjectDescriptionOffsets, MoreMeshesThanDescriptionsDoesNotOverrun)
{
    // meshCountPerModel claims 5 meshes but only 2 descriptions exist - the
    // path ASan would catch if the loop kept writing past the end.
    std::vector<ObjectDescription> descriptions(2);
    const std::vector<uint32_t> meshCountPerModel{ 5 };
    const std::vector<uint32_t> textureCountPerModel{ 3 };

    assignTextureOffsets(descriptions, meshCountPerModel, textureCountPerModel);

    EXPECT_EQ(descriptions[0].texture_offset, 0U);
    EXPECT_EQ(descriptions[1].texture_offset, 0U);
}
