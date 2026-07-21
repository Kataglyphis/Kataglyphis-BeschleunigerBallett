// The CPU half of glTF loading, exercised with NO Vulkan device - the parallel
// of objParseSuite for GltfLoader::parseCpu. Both loaders emit the same
// Vertex/index/ObjMaterial/materialIndex arrays, so this asserts the shared
// invariants (whole triangles, per-face material ids) rather than a byte-exact
// decode. The asset is the Rust renderer's cube.glb, copied in - the point of
// the feature is that both renderers load the SAME glTF.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

import kataglyphis.vulkan.gltf_loader;
import kataglyphis.vulkan.scene_config;

namespace {

std::string test_gltf()
{
    return sceneConfig::resolveModelPath("Models/GltfTest/cube.glb");
}

}// namespace

TEST(GltfParseUnit, ParsesACubeWithoutAVulkanDevice)
{
    if (!std::filesystem::exists(test_gltf())) { GTEST_SKIP() << "test glb not present"; }

    Kataglyphis::GltfLoader loader;// no device: this is the CPU-only path
    ASSERT_TRUE(loader.parseCpu(test_gltf()));

    EXPECT_GT(loader.getVertices().size(), 0U);
    EXPECT_GT(loader.getIndices().size(), 0U);
    EXPECT_EQ(loader.getIndices().size() % 3U, 0U) << "indices must form whole triangles";
    EXPECT_GE(loader.getVertices().size(), 8U) << "a cube has at least eight corners";
}

TEST(GltfParseUnit, MaterialsAndPerFaceIndexAreConsistent)
{
    if (!std::filesystem::exists(test_gltf())) { GTEST_SKIP() << "test glb not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(test_gltf()));

    EXPECT_GT(loader.getMaterials().size(), 0U) << "at least the neutral fallback";
    EXPECT_EQ(loader.getMaterialIndices().size(), loader.getIndices().size() / 3U)
      << "materialIndex is one id per triangle, like the OBJ path";
    for (unsigned int id : loader.getMaterialIndices()) {
        EXPECT_LT(id, loader.getMaterials().size()) << "every face id indexes a real material";
    }
}

TEST(GltfParseUnit, AMissingFileFailsInsteadOfCrashing)
{
    Kataglyphis::GltfLoader loader;
    EXPECT_FALSE(loader.parseCpu("this/path/does/not/exist.glb"));
    EXPECT_TRUE(loader.getVertices().empty());
    EXPECT_TRUE(loader.getIndices().empty());
}
