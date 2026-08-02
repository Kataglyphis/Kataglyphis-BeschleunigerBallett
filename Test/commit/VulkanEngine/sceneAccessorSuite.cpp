// Scene's per-mesh accessors (getVertexBuffer/getIndexBuffer/getIndexCount/
// isMeshDoubleSided/getMeshBounds) bounds-check the model index and then
// index a mesh with no check at all - Model::getMesh(index) used to be a raw
// &vector::operator[], i.e. out-of-range = UB. These pin the safe fallback
// for a model with zero meshes, and the existing add_model(nullptr) guard.

#include <gtest/gtest.h>

#include <memory>
#include <vulkan/vulkan.hpp>

import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.model;

using Kataglyphis::Model;
using Kataglyphis::Scene;

TEST(SceneAccessorUnit, OutOfRangeMeshIndexReturnsTheSafeFallback)
{
    Scene scene;
    scene.add_model(std::make_shared<Model>());

    EXPECT_EQ(scene.getMeshCount(0), 0U);
    EXPECT_EQ(scene.getIndexCount(0, 0), 0U);
    EXPECT_FALSE(scene.isMeshDoubleSided(0, 0));
    EXPECT_EQ(scene.getVertexBuffer(0, 0), vk::Buffer{});
    EXPECT_EQ(scene.getIndexBuffer(0, 0), vk::Buffer{});

    const auto &bounds = scene.getMeshBounds(0, 0);
    EXPECT_GT(bounds.min.x, bounds.max.x) << "an out-of-range mesh must return the inverted 'unknown' box";
}

TEST(SceneAccessorUnit, AddModelIgnoresNull)
{
    Scene scene;
    scene.add_model(nullptr);
    EXPECT_EQ(scene.getModelCount(), 0U) << "a null model (a failed load upstream) must leave the scene unchanged";
}

TEST(SceneAccessorUnit, PerModelCountVectorsAreSizedByModelCount)
{
    Scene scene;

    EXPECT_EQ(scene.getTextureCountPerModel().size(), 0U);
    EXPECT_EQ(scene.getMeshCountPerModel().size(), 0U);

    scene.add_model(std::make_shared<Model>());
    scene.add_model(std::make_shared<Model>());

    const std::vector<uint32_t> texture_counts = scene.getTextureCountPerModel();
    const std::vector<uint32_t> mesh_counts = scene.getMeshCountPerModel();

    ASSERT_EQ(texture_counts.size(), scene.getModelCount());
    ASSERT_EQ(mesh_counts.size(), scene.getModelCount());

    for (uint32_t i = 0; i < scene.getModelCount(); ++i) {
        EXPECT_EQ(texture_counts[i], scene.getTextureCount(i))
          << "the vector form and the indexed form must never drift, model " << i;
        EXPECT_EQ(mesh_counts[i], scene.getMeshCount(i))
          << "the vector form and the indexed form must never drift, model " << i;
    }
}
