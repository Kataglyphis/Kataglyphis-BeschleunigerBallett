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
import kataglyphis.vulkan.obj_material;
import kataglyphis.vulkan.scene_config;

namespace {

std::string test_gltf()
{
    return sceneConfig::resolveModelPath("Models/GltfTest/cube.glb");
}

std::string test_textured_gltf()
{
    return sceneConfig::resolveModelPath("Models/GltfTest/cube_textured.gltf");
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

TEST(GltfParseUnit, ExtractsAnEmbeddedBaseColorTexture)
{
    // cube_textured.gltf carries its base-colour image as a base64 data URI.
    // This is the CPU half of increment d: parseCpu must pull the encoded bytes
    // out (no device needed); the GPU upload is loadModel's job and untested
    // here.
    if (!std::filesystem::exists(test_textured_gltf())) { GTEST_SKIP() << "textured test gltf not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(test_textured_gltf()));

    ASSERT_EQ(loader.getTextureImages().size(), 1U) << "the one material has one base-colour texture";
    const std::vector<unsigned char> &encoded = loader.getTextureImages()[0];

    // The decoded bytes must begin with the 8-byte PNG signature - proof the
    // base64 data-URI decode landed on real image data, not garbage.
    ASSERT_GE(encoded.size(), 8U);
    const unsigned char png_magic[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    for (size_t i = 0; i < 8; ++i) { EXPECT_EQ(encoded[i], png_magic[i]) << "PNG signature byte " << i; }

    // Some material points at the extracted texture, and every id is in range.
    bool anyTextured = false;
    for (const ObjMaterial &material : loader.getMaterials()) {
        if (material.textureID >= 0) {
            EXPECT_LT(static_cast<size_t>(material.textureID), loader.getTextureImages().size());
            anyTextured = true;
        }
    }
    EXPECT_TRUE(anyTextured) << "a textured material must reference the extracted texture";
}
