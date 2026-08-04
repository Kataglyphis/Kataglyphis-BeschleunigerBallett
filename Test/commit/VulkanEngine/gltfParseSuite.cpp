// The CPU half of glTF loading, exercised with NO Vulkan device - the parallel
// of objParseSuite for GltfLoader::parseCpu. Both loaders emit the same
// Vertex/index/ObjMaterial/materialIndex arrays, so this asserts the shared
// invariants (whole triangles, per-face material ids) rather than a byte-exact
// decode. The asset is the Rust renderer's cube.glb, copied in - the point of
// the feature is that both renderers load the SAME glTF.

#include <gtest/gtest.h>

#include <cmath>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vulkan/vulkan.hpp>

import kataglyphis.vulkan.gltf_loader;
import kataglyphis.vulkan.vertex;
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

TEST(GltfParseUnit, MaterialsSharingAnImageShareOneTextureSlot)
{
    // Two materials whose baseColorTexture both name textures[0] -> images[0]:
    // the second must reuse the first's textureImages slot instead of
    // decoding and appending a duplicate.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [
        { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } },
        { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } }
      ],
      "textures": [ { "source": 0 } ],
      "images": [
        { "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGPQuGPz/47Gif8MIALEAQBX2AoVR8sp2gAAAABJRU5ErkJggg==" }
      ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 }
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_shared_image.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getTextureImages().size(), 1U) << "the shared image must be extracted only once";
    ASSERT_EQ(loader.getMaterials().size(), 3U) << "the two declared materials, plus the neutral fallback";
    EXPECT_EQ(loader.getMaterials()[0].textureID, 0);
    EXPECT_EQ(loader.getMaterials()[1].textureID, 0) << "the second material must share slot 0, not get its own";
}

TEST(GltfParseUnit, DistinctImagesStillGetDistinctSlots)
{
    // The negative case for the memoisation above: two materials pointing at
    // two DIFFERENT declared images (even with identical bytes) must not be
    // collapsed into one slot.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [
        { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } },
        { "pbrMetallicRoughness": { "baseColorTexture": { "index": 1 } } }
      ],
      "textures": [ { "source": 0 }, { "source": 1 } ],
      "images": [
        { "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGPQuGPz/47Gif8MIALEAQBX2AoVR8sp2gAAAABJRU5ErkJggg==" },
        { "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGPQuGPz/47Gif8MIALEAQBX2AoVR8sp2gAAAABJRU5ErkJggg==" }
      ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 }
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_distinct_images.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getTextureImages().size(), 2U) << "two distinct declared images must both be extracted";
    ASSERT_EQ(loader.getMaterials().size(), 3U) << "the two declared materials, plus the neutral fallback";
    EXPECT_EQ(loader.getMaterials()[0].textureID, 0);
    EXPECT_EQ(loader.getMaterials()[1].textureID, 1);
}

TEST(GltfParseUnit, EmissiveTextureGetsItsOwnTextureSlot)
{
    // A material whose baseColorTexture and emissiveTexture name two
    // DIFFERENT declared images must get two distinct slots.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [
        {
          "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } },
          "emissiveTexture": { "index": 1 },
          "emissiveFactor": [1, 1, 1]
        }
      ],
      "textures": [ { "source": 0 }, { "source": 1 } ],
      "images": [
        { "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGPQuGPz/47Gif8MIALEAQBX2AoVR8sp2gAAAABJRU5ErkJggg==" },
        { "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGPQuGPz/47Gif8MIALEAQBX2AoVR8sp2gAAAABJRU5ErkJggg==" }
      ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 }
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_distinct_emissive_texture.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getTextureImages().size(), 2U) << "the two distinct declared images must both be extracted";
    ASSERT_EQ(loader.getMaterials().size(), 2U) << "the one declared material, plus the neutral fallback";
    EXPECT_EQ(loader.getMaterials()[0].textureID, 0);
    EXPECT_EQ(loader.getMaterials()[0].emissiveTextureID, 1);
    EXPECT_NE(loader.getMaterials()[0].textureID, loader.getMaterials()[0].emissiveTextureID)
      << "distinct images must not collapse onto one slot";
}

TEST(GltfParseUnit, EmissiveAndBaseColourSharingOneImageShareOneSlot)
{
    // A material whose baseColorTexture and emissiveTexture both name
    // texture[0] -> image[0] must land on ONE textureImages slot: slots are
    // the shared 128-entry descriptor budget.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [
        {
          "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } },
          "emissiveTexture": { "index": 0 },
          "emissiveFactor": [1, 1, 1]
        }
      ],
      "textures": [ { "source": 0 } ],
      "images": [
        { "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGPQuGPz/47Gif8MIALEAQBX2AoVR8sp2gAAAABJRU5ErkJggg==" }
      ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 }
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_shared_emissive_texture.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getTextureImages().size(), 1U) << "the shared image must be extracted only once";
    ASSERT_EQ(loader.getMaterials().size(), 2U) << "the one declared material, plus the neutral fallback";
    EXPECT_EQ(loader.getMaterials()[0].textureID, 0);
    EXPECT_EQ(loader.getMaterials()[0].emissiveTextureID, 0)
      << "base-colour and emissive naming the same (image, sampler) pair must share one slot";
    ASSERT_EQ(loader.getTextureSrgbFlags().size(), loader.getTextureImages().size());
    EXPECT_EQ(loader.getTextureSrgbFlags()[0], 1)
      << "base-colour and emissive are both sRGB, so sharing a slot is still correct under the colour-space key";
}

TEST(GltfParseUnit, MaterialWithoutAnEmissiveTextureKeepsTheSentinel)
{
    // A material with a baseColorTexture but no emissiveTexture must keep
    // ObjMaterial::emissiveTextureID at its -1 sentinel.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [
        { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } }
      ],
      "textures": [ { "source": 0 } ],
      "images": [
        { "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGPQuGPz/47Gif8MIALEAQBX2AoVR8sp2gAAAABJRU5ErkJggg==" }
      ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 }
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_no_emissive_texture.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getMaterials().size(), 2U) << "the one declared material, plus the neutral fallback";
    EXPECT_EQ(loader.getMaterials()[0].textureID, 0);
    EXPECT_EQ(loader.getMaterials()[0].emissiveTextureID, -1)
      << "a material without an emissiveTexture must keep the -1 sentinel";
}

TEST(GltfParseUnit, NormalTextureGetsItsOwnTextureSlot)
{
    // A material whose baseColorTexture and normalTexture name two DIFFERENT
    // declared images must get two distinct slots.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [
        {
          "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } },
          "normalTexture": { "index": 1 }
        }
      ],
      "textures": [ { "source": 0 }, { "source": 1 } ],
      "images": [
        { "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGPQuGPz/47Gif8MIALEAQBX2AoVR8sp2gAAAABJRU5ErkJggg==" },
        { "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGPQuGPz/47Gif8MIALEAQBX2AoVR8sp2gAAAABJRU5ErkJggg==" }
      ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 }
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_distinct_normal_texture.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getTextureImages().size(), 2U) << "the two distinct declared images must both be extracted";
    ASSERT_EQ(loader.getMaterials().size(), 2U) << "the one declared material, plus the neutral fallback";
    EXPECT_EQ(loader.getMaterials()[0].textureID, 0);
    EXPECT_EQ(loader.getMaterials()[0].normalTextureID, 1);
    EXPECT_NE(loader.getMaterials()[0].textureID, loader.getMaterials()[0].normalTextureID)
      << "distinct images must not collapse onto one slot";
    ASSERT_EQ(loader.getTextureSrgbFlags().size(), loader.getTextureImages().size());
    EXPECT_EQ(loader.getTextureSrgbFlags()[loader.getMaterials()[0].textureID], 1)
      << "base-colour texel data is sRGB-encoded";
    EXPECT_EQ(loader.getTextureSrgbFlags()[loader.getMaterials()[0].normalTextureID], 0)
      << "normal-map texel data is linear tangent-space offsets, not gamma-encoded colour";
}

TEST(GltfParseUnit, NormalAndBaseColourSharingOneImageGetTwoSlotsForDifferentColourSpaces)
{
    // A material whose baseColorTexture and normalTexture both name
    // texture[0] -> image[0] must nonetheless land on TWO textureImages
    // slots: a slot can only carry one VkFormat, and base-colour (sRGB) and
    // normal-map (linear/UNORM) data need different formats.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [
        {
          "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } },
          "normalTexture": { "index": 0 }
        }
      ],
      "textures": [ { "source": 0 } ],
      "images": [
        { "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGPQuGPz/47Gif8MIALEAQBX2AoVR8sp2gAAAABJRU5ErkJggg==" }
      ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 }
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_shared_normal_texture.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getTextureImages().size(), 2U)
      << "the shared image must still be extracted twice: one slot can only carry one image format, and "
         "base-colour (sRGB) and normal (linear) need different formats";
    ASSERT_EQ(loader.getMaterials().size(), 2U) << "the one declared material, plus the neutral fallback";
    EXPECT_EQ(loader.getMaterials()[0].textureID, 0);
    EXPECT_EQ(loader.getMaterials()[0].normalTextureID, 1);
    EXPECT_NE(loader.getMaterials()[0].textureID, loader.getMaterials()[0].normalTextureID)
      << "one (image, sampler) pair used at two colour spaces must not collapse onto one slot";
    ASSERT_EQ(loader.getTextureSrgbFlags().size(), loader.getTextureImages().size());
    EXPECT_EQ(loader.getTextureSrgbFlags()[loader.getMaterials()[0].textureID], 1);
    EXPECT_EQ(loader.getTextureSrgbFlags()[loader.getMaterials()[0].normalTextureID], 0);
}

TEST(GltfParseUnit, MaterialWithoutANormalTextureKeepsTheSentinel)
{
    // A material with a baseColorTexture but no normalTexture must keep
    // ObjMaterial::normalTextureID at its -1 sentinel.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [
        { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } }
      ],
      "textures": [ { "source": 0 } ],
      "images": [
        { "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGPQuGPz/47Gif8MIALEAQBX2AoVR8sp2gAAAABJRU5ErkJggg==" }
      ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 }
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_no_normal_texture.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getMaterials().size(), 2U) << "the one declared material, plus the neutral fallback";
    EXPECT_EQ(loader.getMaterials()[0].textureID, 0);
    EXPECT_EQ(loader.getMaterials()[0].normalTextureID, -1)
      << "a material without a normalTexture must keep the -1 sentinel";
    EXPECT_FLOAT_EQ(loader.getMaterials()[0].normalScale, 1.0F)
      << "a material without a normalTexture must keep normalScale unscaled, even though cgltf leaves "
         "cgltf_texture_view::scale zero-initialized in that case";
}

TEST(GltfParseUnit, NormalTextureScaleIsCarriedIntoTheMaterial)
{
    // A material with an authored normalTexture.scale must carry it through
    // to ObjMaterial::normalScale.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [
        {
          "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } },
          "normalTexture": { "index": 1, "scale": 0.5 }
        }
      ],
      "textures": [ { "source": 0 }, { "source": 1 } ],
      "images": [
        { "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGPQuGPz/47Gif8MIALEAQBX2AoVR8sp2gAAAABJRU5ErkJggg==" },
        { "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGPQuGPz/47Gif8MIALEAQBX2AoVR8sp2gAAAABJRU5ErkJggg==" }
      ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 }
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_normal_texture_scale.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getMaterials().size(), 2U) << "the one declared material, plus the neutral fallback";
    EXPECT_FLOAT_EQ(loader.getMaterials()[0].normalScale, 0.5F);
}

TEST(GltfParseUnit, ReadsSamplerWrapAndFilterFromTheDocument)
{
    // A texture naming an explicit sampler must have that sampler's wrap and
    // filter settings show up in getTextureSamplerDescs(), index-parallel with
    // getTextureImages().
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [
        { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } }
      ],
      "textures": [ { "source": 0, "sampler": 0 } ],
      "samplers": [ { "wrapS": 33071, "wrapT": 33071, "magFilter": 9728 } ],
      "images": [
        { "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGPQuGPz/47Gif8MIALEAQBX2AoVR8sp2gAAAABJRU5ErkJggg==" }
      ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 }
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_sampler_wrap_filter.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getTextureSamplerDescs().size(), 1U);
    const Kataglyphis::GltfSamplerDesc &desc = loader.getTextureSamplerDescs()[0];
    EXPECT_EQ(desc.addressModeU, vk::SamplerAddressMode::eClampToEdge);
    EXPECT_EQ(desc.addressModeV, vk::SamplerAddressMode::eClampToEdge);
    EXPECT_EQ(desc.magFilter, vk::Filter::eNearest);
    // minFilter was not set by the document - defaults survive untouched.
    EXPECT_EQ(desc.minFilter, vk::Filter::eLinear);
    EXPECT_EQ(desc.mipmapMode, vk::SamplerMipmapMode::eLinear);
}

TEST(GltfParseUnit, OneImageWithTwoSamplersGetsTwoSlots)
{
    // Two textures share the same image but name different samplers: the
    // dedup key is (image, sampler), so this must NOT collapse onto one
    // textureImages slot the way MaterialsSharingAnImageShareOneTextureSlot's
    // identical-sampler case does.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [
        { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } },
        { "pbrMetallicRoughness": { "baseColorTexture": { "index": 1 } } }
      ],
      "textures": [ { "source": 0, "sampler": 0 }, { "source": 0, "sampler": 1 } ],
      "samplers": [
        { "wrapS": 10497, "wrapT": 10497 },
        { "wrapS": 33071, "wrapT": 33071, "magFilter": 9728 }
      ],
      "images": [
        { "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGPQuGPz/47Gif8MIALEAQBX2AoVR8sp2gAAAABJRU5ErkJggg==" }
      ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 }
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_two_samplers_one_image.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getTextureImages().size(), 2U) << "same image, different sampler must still get two slots";
    ASSERT_EQ(loader.getTextureSamplerDescs().size(), 2U);
    ASSERT_EQ(loader.getMaterials().size(), 3U) << "the two declared materials, plus the neutral fallback";
    EXPECT_EQ(loader.getMaterials()[0].textureID, 0);
    EXPECT_EQ(loader.getMaterials()[1].textureID, 1);
    EXPECT_EQ(loader.getTextureSamplerDescs()[0].addressModeU, vk::SamplerAddressMode::eRepeat);
    EXPECT_EQ(loader.getTextureSamplerDescs()[1].addressModeU, vk::SamplerAddressMode::eClampToEdge);
    EXPECT_EQ(loader.getTextureSamplerDescs()[1].magFilter, vk::Filter::eNearest);
}

TEST(GltfParseUnit, BaseColorFactorSurvivesATexturedMaterial)
{
    // glTF base colour = baseColorFactor * sampled texture. fromGltfMaterial
    // must keep the factor in ObjMaterial::diffuse even when a texture is also
    // present - the shaders (material_fetch.slang's base_color()) rely on both
    // the factor and the textureID surviving parseCpu together.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [
        { "pbrMetallicRoughness": {
            "baseColorFactor": [0.25, 0.5, 1.0, 1.0],
            "baseColorTexture": { "index": 0 }
        } }
      ],
      "textures": [ { "source": 0 } ],
      "images": [
        { "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGPQuGPz/47Gif8MIALEAQBX2AoVR8sp2gAAAABJRU5ErkJggg==" }
      ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 },
        "material": 0
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_base_color_factor.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getMaterials().size(), 2U) << "the one declared material, plus the neutral fallback";
    const ObjMaterial &material = loader.getMaterials()[0];
    EXPECT_GE(material.textureID, 0) << "the material must still reference its base-colour texture";
    EXPECT_NEAR(material.diffuse.x, 0.25F, 1e-5F);
    EXPECT_NEAR(material.diffuse.y, 0.5F, 1e-5F);
    EXPECT_NEAR(material.diffuse.z, 1.0F, 1e-5F);
}

// Untrusted-input hardening (2026-07-22): the GUI model picker feeds arbitrary
// files to parseCpu. These prove the structural guards reject rather than
// crash. The full coverage-guided sweep is gltf_parsing_fuzz_test; these pin
// the specific defects the survey named.
TEST(GltfParseUnit, MalformedTextIsRejectedNotCrashed)
{
    Kataglyphis::GltfLoader loader;

    // Truncated/garbage JSON must fail parse, not walk a half-built document.
    const auto tmp = std::filesystem::temp_directory_path() / "kat_bad_gltf.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << "{ \"asset\": { \"version\": \"2.0\" ";// unterminated
    }
    EXPECT_FALSE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);
}

TEST(GltfParseUnit, ShortBase64ImageUriDoesNotUnderflow)
{
    // A base64 image data-URI shorter than one quad used to make the decoded-
    // length maths (b64len/4*3 - padding) UNDERFLOW to ~SIZE_MAX, requesting a
    // gigantic read from a one-character URI. The document is otherwise valid,
    // so it parses and validates; extraction must simply yield no texture.
    const char *doc = R"({
      "asset": { "version": "2.0" },
      "images": [ { "uri": "data:image/png;base64,QQ" } ],
      "textures": [ { "source": 0 } ],
      "materials": [ { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } } ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 },
        "material": 0
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,1], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_short_b64.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }
    Kataglyphis::GltfLoader loader;
    // The point is no crash / no OOB (ASan-enforced in CI); whether the parse
    // succeeds, it must extract zero textures from the malformed URI.
    const bool parsed = loader.parseCpu(tmp.string());
    if (parsed) {
        EXPECT_TRUE(loader.getTextureImages().empty())
          << "a sub-quad base64 URI must yield no texture, not an underflowed read";
    }
    std::filesystem::remove(tmp);
}

namespace {

// A minimal-but-real PNG: the 8-byte signature plus a truncated IHDR chunk
// header. extractImageBytes never decodes it (that is Texture::createFromMemory's
// job in uploadParsed, untested here) - only that the bytes made it through
// unmodified, so a short-but-genuine PNG prefix is enough.
const unsigned char kMinimalPngBytes[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,// PNG signature
    0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52// IHDR length + tag
};

// The same accessors/bufferViews/buffers skeleton ShortBase64ImageUriDoesNotUnderflow
// uses (one triangle, irrelevant to the image path); only the "images" entry's
// uri differs per test.
std::string externalImageGltfDoc(const std::string &imageUri)
{
    return "{\n"
           "  \"asset\": { \"version\": \"2.0\" },\n"
           "  \"images\": [ { \"uri\": \""
      + imageUri
      + "\" } ],\n"
        "  \"textures\": [ { \"source\": 0 } ],\n"
        "  \"materials\": [ { \"pbrMetallicRoughness\": { \"baseColorTexture\": { \"index\": 0 } } } ],\n"
        "  \"meshes\": [ { \"primitives\": [ {\n"
        "    \"attributes\": { \"POSITION\": 0 },\n"
        "    \"material\": 0\n"
        "  } ] } ],\n"
        "  \"nodes\": [ { \"mesh\": 0 } ],\n"
        "  \"scenes\": [ { \"nodes\": [ 0 ] } ],\n"
        "  \"accessors\": [ { \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\",\n"
        "                   \"min\": [0,0,0], \"max\": [1,1,1], \"bufferView\": 0 } ],\n"
        "  \"bufferViews\": [ { \"buffer\": 0, \"byteLength\": 36 } ],\n"
        "  \"buffers\": [ { \"byteLength\": 36,\n"
        "    \"uri\": \"data:application/octet-stream;base64,"
        "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA\" } ]\n"
        "}\n";
}

void writeFile(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

void writePngFile(const std::filesystem::path &path)
{
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(kMinimalPngBytes), sizeof(kMinimalPngBytes));
}

}// namespace

TEST(GltfParseUnit, ExternalImageUriIsResolvedRelativeToTheDocument)
{
    const auto dir = std::filesystem::temp_directory_path() / "kat_gltf_ext_ok";
    std::filesystem::create_directories(dir);
    writePngFile(dir / "tex.png");
    writeFile(dir / "model.gltf", externalImageGltfDoc("tex.png"));

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu((dir / "model.gltf").string()));

    ASSERT_EQ(loader.getTextureImages().size(), 1U);
    EXPECT_FALSE(loader.getTextureImages()[0].empty());
    bool anyTextured = false;
    for (const ObjMaterial &material : loader.getMaterials()) {
        if (material.textureID >= 0) {
            EXPECT_EQ(material.textureID, 0);
            anyTextured = true;
        }
    }
    EXPECT_TRUE(anyTextured);

    std::filesystem::remove_all(dir);
}

TEST(GltfParseUnit, ExternalImageUriEscapingTheDocumentDirectoryIsRejected)
{
    const auto parentDir = std::filesystem::temp_directory_path() / "kat_gltf_ext_escape";
    const auto docDir = parentDir / "doc";
    std::filesystem::create_directories(docDir);
    writePngFile(parentDir / "secret.png");
    writeFile(docDir / "model.gltf", externalImageGltfDoc("../secret.png"));

    Kataglyphis::GltfLoader loader;
    const bool parsed = loader.parseCpu((docDir / "model.gltf").string());
    if (parsed) {
        EXPECT_TRUE(loader.getTextureImages().empty())
          << "a URI that escapes the document directory must yield no texture";
        for (const ObjMaterial &material : loader.getMaterials()) { EXPECT_EQ(material.textureID, -1); }
    }

    std::filesystem::remove_all(parentDir);
}

TEST(GltfParseUnit, PercentEncodedExternalUriResolves)
{
    const auto dir = std::filesystem::temp_directory_path() / "kat_gltf_ext_percent";
    std::filesystem::create_directories(dir);
    writePngFile(dir / "my tex.png");
    writeFile(dir / "model.gltf", externalImageGltfDoc("my%20tex.png"));

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu((dir / "model.gltf").string()));

    ASSERT_EQ(loader.getTextureImages().size(), 1U);
    EXPECT_FALSE(loader.getTextureImages()[0].empty());

    std::filesystem::remove_all(dir);
}

TEST(GltfParseUnit, MissingNormalsAreComputedFlatNotDefaultedUp)
{
    // glTF spec: when NORMAL is absent, the implementation MUST compute flat
    // normals. The loader used to default every such vertex to (0,1,0), so a
    // normal-less mesh lit as if every face pointed straight up. This asset is
    // one triangle in the XY plane - its true flat normal is (0,0,+/-1), which
    // the old default (0,1,0) could never produce.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 }
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_no_normal.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_GE(loader.getVertices().size(), 3U);
    // Every emitted normal must be the triangle's geometric normal, |Z| == 1,
    // and must NOT be the old (0,1,0) default.
    for (const Vertex &v : loader.getVertices()) {
        const glm::vec3 n = v.normal;
        EXPECT_GT(std::abs(n.z), 0.99F) << "computed flat normal should be +/-Z for an XY-plane triangle";
        EXPECT_LT(std::abs(n.y), 0.01F) << "normal defaulted to up (0,1,0) - flat computation did not run";
    }
}

TEST(GltfParseUnit, TriangleStripIsTriangulatedNotDropped)
{
    // A TRIANGLE_STRIP primitive (mode 5) used to be skipped by the
    // triangles-only check, silently dropping any mesh exported that way. A
    // 4-vertex strip triangulates to 2 triangles (6 indices).
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 },
        "mode": 5
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 4, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 48 } ],
      "buffers": [ { "byteLength": 48,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAACAPwAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_strip.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }
    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()))
      << "a triangle-strip mesh must load, not be dropped as non-triangle";
    std::filesystem::remove(tmp);

    EXPECT_EQ(loader.getVertices().size(), 4U) << "four strip vertices";
    EXPECT_EQ(loader.getIndices().size(), 6U) << "a 4-vertex strip triangulates to 2 triangles";
    EXPECT_EQ(loader.getIndices().size() % 3U, 0U);
}

TEST(GltfParseUnit, TriangleFanIsTriangulatedAroundTheHubVertex)
{
    // A TRIANGLE_FAN primitive (mode 6) triangulates with vertex 0 shared by
    // every triangle: a 4-vertex fan -> triangles (0,1,2) and (0,2,3), i.e.
    // 6 indices. Fans, like strips, used to be dropped by the triangles-only
    // gate. This pins the fan WINDING too, so the parseCpu -> processPrimitive
    // split cannot silently regress a fan to the strip pattern (whose second
    // triangle is (2,1,3), not (0,2,3)).
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 },
        "mode": 6
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 4, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 48 } ],
      "buffers": [ { "byteLength": 48,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAACAPwAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_fan.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }
    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()))
      << "a triangle-fan mesh must load, not be dropped as non-triangle";
    std::filesystem::remove(tmp);

    EXPECT_EQ(loader.getVertices().size(), 4U) << "four fan vertices";
    ASSERT_EQ(loader.getIndices().size(), 6U) << "a 4-vertex fan triangulates to 2 triangles";
    const std::vector<unsigned int> expected = { 0U, 1U, 2U, 0U, 2U, 3U };
    EXPECT_EQ(loader.getIndices(), expected)
      << "a fan must triangulate around the shared hub vertex 0, not with the strip winding";
}

TEST(GltfParseUnit, OutOfRangeIndicesDropTheirTriangleNotTheMesh)
{
    // A malformed glTF can carry index values that don't address any vertex
    // the primitive actually shipped (fuzz-found hazard, mirrors the OBJ
    // loader's face_valid guard). Positions: 3 vertices. Indices: two
    // triangles, the second of which references vertex 7 - out of range for
    // a 3-vertex primitive. The whole triangle must be dropped, not just the
    // bad corner (materialIndex is one id per triangle, and indices must
    // stay a multiple of 3). The fixture also carries no NORMAL attribute, so
    // the flat-normal pass below runs on the SANITIZED index list - under
    // clangcl-debug (ASAN) the pre-fix version is an out-of-bounds write into
    // `vertices`, making this the ASAN oracle for the guard.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 },
        "indices": 1
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [
        { "componentType": 5126, "count": 3, "type": "VEC3",
          "min": [0,0,0], "max": [1,1,0], "bufferView": 0 },
        { "componentType": 5123, "count": 6, "type": "SCALAR", "bufferView": 1 }
      ],
      "bufferViews": [
        { "buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962 },
        { "buffer": 0, "byteOffset": 36, "byteLength": 12, "target": 34963 }
      ],
      "buffers": [ { "byteLength": 48,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIABwABAAIA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_oob_index.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }
    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()))
      << "an out-of-range index must drop its triangle, not fail the whole mesh";
    std::filesystem::remove(tmp);

    EXPECT_EQ(loader.getVertices().size(), 3U);
    EXPECT_EQ(loader.getIndices().size(), 3U) << "only the first (valid) triangle survives";
    EXPECT_EQ(loader.getIndices().size() % 3U, 0U);
    for (unsigned int idx : loader.getIndices()) {
        EXPECT_LT(idx, loader.getVertices().size()) << "every surviving index addresses a real vertex";
    }
    EXPECT_EQ(loader.getMaterialIndices().size(), loader.getIndices().size() / 3U)
      << "material-id count must equal the surviving triangle count";
}

namespace {

// A minimal one-triangle glTF (same POSITION buffer as the tests above: three
// vertices spanning x in [0,1]) whose node either does or does not carry a
// skin, so the fixture can pin whether the node's world transform is applied.
std::string skin_node_gltf(bool skinned)
{
    const std::string skinBlock = skinned ? R"GLTF(, "skin": 0)GLTF" : "";
    const std::string skinsArray = skinned ? R"GLTF("skins": [ { "joints": [ 1 ] } ],)GLTF" : "";
    return std::string(R"GLTF({
      "asset": { "version": "2.0" },
      )GLTF")
      + skinsArray + R"GLTF(
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 }
      } ] } ],
      "nodes": [ { "mesh": 0, "translation": [10, 0, 0])GLTF"
      + skinBlock + R"GLTF( }, { } ],
      "scenes": [ { "nodes": [ 0, 1 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
}

// A minimal one-triangle glTF whose single material carries the given
// "alphaMode"/"alphaCutoff" snippet. The 36-byte POSITION buffer is the same
// three-vertex block the tests above reuse.
std::string material_gltf(const std::string &alphaSnippet)
{
    return std::string(R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [ { "pbrMetallicRoughness": { "baseColorFactor": [1,1,1,1] })GLTF")
      + alphaSnippet + R"GLTF( } ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 },
        "material": 0
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
}

float first_material_cutoff(const std::string &doc, const char *tmpName)
{
    const auto tmp = std::filesystem::temp_directory_path() / tmpName;
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }
    Kataglyphis::GltfLoader loader;
    const bool parsed = loader.parseCpu(tmp.string());
    std::filesystem::remove(tmp);
    EXPECT_TRUE(parsed);
    EXPECT_GT(loader.getMaterials().size(), 0U);
    return loader.getMaterials().empty() ? 0.0F : loader.getMaterials()[0].alphaCutoff;
}

}// namespace

TEST(GltfParseUnit, SkinnedNodeTransformIsIgnored)
{
    // glTF 2.0 spec (Skins): "the transform of the skinned mesh node MUST be
    // ignored" - only joint transforms position a skinned mesh, and this
    // engine has no joint animation, so a skinned node's vertices must stay
    // in bind pose. Red without the GltfLoader change: the +10 translation
    // would still bake into the vertices, same as the unskinned control below.
    const auto tmp = std::filesystem::temp_directory_path() / "kat_skinned_translated.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << skin_node_gltf(/*skinned=*/true);
    }
    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_GE(loader.getVertices().size(), 3U);
    for (const Vertex &v : loader.getVertices()) {
        EXPECT_LT(v.position.x, 2.0F) << "a skinned node's translation must be ignored - vertex stayed in bind pose";
    }
}

TEST(GltfParseUnit, UnskinnedNodeTransformStillApplies)
{
    // Control for the test above: an UNSKINNED node with the same translation
    // must still move, so the skin check does not accidentally suppress every
    // node transform.
    const auto tmp = std::filesystem::temp_directory_path() / "kat_unskinned_translated.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << skin_node_gltf(/*skinned=*/false);
    }
    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_GE(loader.getVertices().size(), 3U);
    bool anyShifted = false;
    for (const Vertex &v : loader.getVertices()) {
        if (v.position.x > 9.0F) { anyShifted = true; }
    }
    EXPECT_TRUE(anyShifted) << "an unskinned node's translation must still apply to its vertices";
}

namespace {

// A minimal one-triangle glTF (same 36-byte POSITION buffer as the fixtures
// above: three vertices spanning x/y in [0,1]) whose node carries the given
// "scale", so a test can pin whether a negative-determinant transform
// reverses the emitted winding.
std::string scaled_node_gltf(const std::string &scaleJson)
{
    return std::string(R"GLTF({
      "asset": { "version": "2.0" },
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 }
      } ] } ],
      "nodes": [ { "mesh": 0, "scale": )GLTF")
      + scaleJson + R"GLTF( } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
}

std::vector<unsigned int> indices_of_scaled_node_gltf(const std::string &scaleJson, const char *tmpName)
{
    const auto tmp = std::filesystem::temp_directory_path() / tmpName;
    {
        std::ofstream out(tmp, std::ios::binary);
        out << scaled_node_gltf(scaleJson);
    }
    Kataglyphis::GltfLoader loader;
    const bool parsed = loader.parseCpu(tmp.string());
    std::filesystem::remove(tmp);
    EXPECT_TRUE(parsed);
    return loader.getIndices();
}

}// namespace

TEST(GltfParseUnit, MirroredNodeReversesTriangleWinding)
{
    // glTF 2.0 spec (3.7.4 Transformations): a negative-determinant node
    // transform must reverse the triangle winding order. "scale": [-1,1,1]
    // has determinant -1 (a mirror), so the emitted list-topology triangle
    // (0,1,2) must come out as (0,2,1). Red without the GltfLoader change:
    // the mirrored mesh keeps (0,1,2), gets back-face-culled by
    // MeshDrawRecorder's eBack, and disappears.
    const auto indices = indices_of_scaled_node_gltf("[-1, 1, 1]", "kat_mirrored.gltf");
    const std::vector<unsigned int> expected = { 0U, 2U, 1U };
    EXPECT_EQ(indices, expected) << "a mirrored (determinant < 0) node must reverse its triangle winding";
}

TEST(GltfParseUnit, UnmirroredNodeKeepsItsWinding)
{
    // Control for the test above: a uniform positive scale (determinant +1)
    // must NOT reverse the winding, so the determinant check cannot silently
    // invert every node's triangles.
    const auto indices = indices_of_scaled_node_gltf("[1, 1, 1]", "kat_unmirrored.gltf");
    const std::vector<unsigned int> expected = { 0U, 1U, 2U };
    EXPECT_EQ(indices, expected) << "an unmirrored node must keep its original triangle winding";
}

TEST(GltfParseUnit, RotatedNodeWithTwoNegativeScalesKeepsItsWinding)
{
    // "scale": [-1,-1,1] has determinant (+1)(-1)(-1)... actually (-1)*(-1)*1
    // = +1: a 180-degree rotation about Z, not a mirror. This is the case a
    // naive "any negative scale component" check gets wrong - only the SIGN
    // of the determinant (an odd number of negative factors) matters.
    const auto indices = indices_of_scaled_node_gltf("[-1, -1, 1]", "kat_rotated_not_mirrored.gltf");
    const std::vector<unsigned int> expected = { 0U, 1U, 2U };
    EXPECT_EQ(indices, expected) << "two negative scale components is a rotation (det +1), winding must be unchanged";
}

TEST(GltfParseUnit, MaskAlphaModeSetsTheCutoff)
{
    // glTF alphaMode MASK carries the cutoff the raster shaders discard against.
    // The loader used to drop it entirely, so a cut-out foliage material rendered
    // as its solid quad. Red without the GltfLoader change: alphaCutoff would be
    // the constructor default (-1), not the asset's 0.5.
    const float cutoff = first_material_cutoff(material_gltf(R"(, "alphaMode": "MASK", "alphaCutoff": 0.5)"),
      "kat_mask.gltf");
    EXPECT_NEAR(cutoff, 0.5F, 1e-6F) << "MASK material must carry its alphaCutoff into ObjMaterial";
}

TEST(GltfParseUnit, OpaqueMaterialHasNoCutoff)
{
    // OPAQUE (the default when alphaMode is absent) and BLEND must map to a
    // negative sentinel so the shaders never discard - otherwise every opaque
    // glTF would punch holes wherever its base-colour alpha dipped.
    const float cutoff = first_material_cutoff(material_gltf(""), "kat_opaque.gltf");
    EXPECT_LT(cutoff, 0.0F) << "a non-MASK material must have alphaCutoff < 0 (never discards)";
}

TEST(GltfParseUnit, BaseColourFactorAlphaReachesTheMaterial)
{
    // glTF baseColorFactor.a is the alpha half of the MASK test
    // (baseColorFactor.a * baseColorTexture.a, texture term defaulting to 1).
    // fromGltfMaterial used to drop the fourth component on the floor and pass
    // a literal 1.0F for dissolve - an untextured MASK material could then
    // never discard, and a textured one ignored the factor's alpha entirely.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [
        { "pbrMetallicRoughness": { "baseColorFactor": [1.0, 1.0, 1.0, 0.25] } }
      ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 },
        "material": 0
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_base_color_factor_alpha.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_GT(loader.getMaterials().size(), 0U);
    EXPECT_NEAR(loader.getMaterials()[0].dissolve, 0.25F, 1e-6F)
      << "baseColorFactor.a must reach ObjMaterial::dissolve";
}

TEST(GltfParseUnit, OpaqueMaterialWithoutFactorStillHasFullDissolve)
{
    // An OPAQUE material whose baseColorFactor alpha is the default 1.0 (as
    // material_gltf's fixture uses) must yield ObjMaterial::dissolve == 1.0F -
    // otherwise every plain glTF would gain a spurious discard once
    // alphaCutoff-based MASK materials exist nearby.
    const float dissolve = [] {
        const auto tmp = std::filesystem::temp_directory_path() / "kat_no_alpha_material.gltf";
        {
            std::ofstream out(tmp, std::ios::binary);
            out << material_gltf("");
        }
        Kataglyphis::GltfLoader loader;
        const bool parsed = loader.parseCpu(tmp.string());
        std::filesystem::remove(tmp);
        EXPECT_TRUE(parsed);
        EXPECT_GT(loader.getMaterials().size(), 0U);
        return loader.getMaterials().empty() ? 0.0F : loader.getMaterials()[0].dissolve;
    }();
    EXPECT_NEAR(dissolve, 1.0F, 1e-6F) << "a material without an explicit alpha factor must be fully opaque";
}

TEST(GltfParseUnit, MetallicFactorReachesTheMaterial)
{
    // glTF pbrMetallicRoughness.metallicFactor must carry through to
    // ObjMaterial::metallic instead of being dropped on the floor - the
    // loader used to have no metallic-roughness slot at all, so every glTF
    // metal rendered as a dielectric.
    const auto path = sceneConfig::resolveModelPath("Models/GltfTest/metallic_card.gltf");
    if (!std::filesystem::exists(path)) { GTEST_SKIP() << "metallic_card fixture not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(path));

    ASSERT_GT(loader.getMaterials().size(), 0U);
    EXPECT_NEAR(loader.getMaterials()[0].metallic, 0.75F, 1e-6F)
      << "the fixture's metallicFactor must reach ObjMaterial::metallic";
}

TEST(GltfParseUnit, MaterialWithoutPbrMetallicRoughnessHasZeroMetallic)
{
    // A material with no pbrMetallicRoughness block at all (cgltf reports
    // has_pbr_metallic_roughness == 0) must not pick up a stray non-zero
    // metallic - the fromGltfMaterial() metallic read is gated behind that
    // same has_pbr_metallic_roughness check as base colour and roughness.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [ {} ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 },
        "material": 0
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_no_pbr_material.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_GT(loader.getMaterials().size(), 0U);
    EXPECT_NEAR(loader.getMaterials()[0].metallic, 0.0F, 1e-6F)
      << "a material without pbrMetallicRoughness must default to metallic 0.0";
}

TEST(GltfParseUnit, RoughnessFactorReachesTheMaterialUnchanged)
{
    // glTF pbrMetallicRoughness.roughnessFactor must carry through losslessly
    // to ObjMaterial::roughness instead of only round-tripping through the
    // shininess approximation - ObjMaterial used to have no roughness slot at
    // all, so this is red without the ObjMaterial change.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [
        { "pbrMetallicRoughness": { "baseColorFactor": [1,1,1,1], "roughnessFactor": 0.5 } }
      ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 },
        "material": 0
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_roughness_factor.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_GT(loader.getMaterials().size(), 0U);
    EXPECT_NEAR(loader.getMaterials()[0].roughness, 0.5F, 1e-5F)
      << "the material's roughnessFactor must reach ObjMaterial::roughness";
}

TEST(GltfParseUnit, MaterialWithoutPbrMetallicRoughnessHasNoAuthoredRoughness)
{
    // A material with no pbrMetallicRoughness block at all (cgltf reports
    // has_pbr_metallic_roughness == 0) must keep ObjMaterial::roughness at its
    // negative sentinel, so material_roughness() still falls back to the
    // shininess-derived approximation for OBJ and no-pbr glTF materials.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [ {} ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 },
        "material": 0
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_no_pbr_material_roughness.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_GT(loader.getMaterials().size(), 0U);
    EXPECT_LT(loader.getMaterials()[0].roughness, 0.0F)
      << "a material without pbrMetallicRoughness must have no authored roughness";
}

TEST(GltfParseUnit, EmissiveFactorReachesTheMaterial)
{
    // glTF material.emissiveFactor must carry through to ObjMaterial::emission -
    // the plain path with no KHR_materials_emissive_strength extension involved.
    const auto path = sceneConfig::resolveModelPath("Models/GltfTest/emissive_card.gltf");
    if (!std::filesystem::exists(path)) { GTEST_SKIP() << "emissive_card fixture not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(path));

    ASSERT_GT(loader.getMaterials().size(), 0U);
    const glm::vec3 &emission = loader.getMaterials()[0].emission;
    EXPECT_NEAR(emission.x, 1.0F, 1e-6F) << "the fixture's emissiveFactor must reach ObjMaterial::emission";
    EXPECT_NEAR(emission.y, 1.0F, 1e-6F) << "the fixture's emissiveFactor must reach ObjMaterial::emission";
    EXPECT_NEAR(emission.z, 1.0F, 1e-6F) << "the fixture's emissiveFactor must reach ObjMaterial::emission";
}

TEST(GltfParseUnit, EmissiveStrengthScalesTheEmissiveFactor)
{
    // KHR_materials_emissive_strength must scale emissiveFactor past the [0,1]
    // glTF range - without folding the strength in, an emissiveStrength of 4.0
    // would still read back as (1,1,1) instead of (4,4,4).
    const auto path = sceneConfig::resolveModelPath("Models/GltfTest/emissive_strength_card.gltf");
    if (!std::filesystem::exists(path)) { GTEST_SKIP() << "emissive_strength_card fixture not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(path));

    ASSERT_GT(loader.getMaterials().size(), 0U);
    const glm::vec3 &emission = loader.getMaterials()[0].emission;
    EXPECT_NEAR(emission.x, 4.0F, 1e-5F) << "emissiveStrength must scale emissiveFactor into ObjMaterial::emission";
    EXPECT_NEAR(emission.y, 4.0F, 1e-5F) << "emissiveStrength must scale emissiveFactor into ObjMaterial::emission";
    EXPECT_NEAR(emission.z, 4.0F, 1e-5F) << "emissiveStrength must scale emissiveFactor into ObjMaterial::emission";
}

TEST(GltfParseUnit, MaterialWithoutEmissiveStrengthIsUnscaled)
{
    // A material with an emissiveFactor but no KHR_materials_emissive_strength
    // extension (cgltf reports has_emissive_strength == 0) must not pick up a
    // stray scale - the emission read stays at the plain factor.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "materials": [
        { "emissiveFactor": [0.2, 0.4, 0.6] }
      ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 },
        "material": 0
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_no_emissive_strength_material.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_GT(loader.getMaterials().size(), 0U);
    const glm::vec3 &emission = loader.getMaterials()[0].emission;
    EXPECT_NEAR(emission.x, 0.2F, 1e-6F) << "a material without emissive_strength must keep the plain factor";
    EXPECT_NEAR(emission.y, 0.4F, 1e-6F) << "a material without emissive_strength must keep the plain factor";
    EXPECT_NEAR(emission.z, 0.6F, 1e-6F) << "a material without emissive_strength must keep the plain factor";
}

TEST(GltfParseUnit, MaskCardFixtureLoadsWithCutoutTextureAndCutoff)
{
    // mask_card.gltf (a quad + a checkerboard-alpha cut-out PNG, alphaMode MASK /
    // cutoff 0.5) is the shared asset the MASK visual + shadow goldens build on.
    // Prove it is well-formed end to end - geometry, the extracted base-colour
    // PNG, and the cutoff all survive parseCpu - so a golden that later fails is
    // the renderer's fault, not a broken fixture.
    const auto path = sceneConfig::resolveModelPath("Models/GltfTest/mask_card.gltf");
    if (!std::filesystem::exists(path)) { GTEST_SKIP() << "mask_card fixture not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(path));

    EXPECT_EQ(loader.getVertices().size(), 4U) << "a quad card has four corners";
    EXPECT_EQ(loader.getIndices().size(), 6U) << "two triangles";

    ASSERT_GT(loader.getMaterials().size(), 0U);
    EXPECT_NEAR(loader.getMaterials()[0].alphaCutoff, 0.5F, 1e-6F)
      << "the fixture's MASK cutoff must reach ObjMaterial";

    ASSERT_EQ(loader.getTextureImages().size(), 1U) << "the one cut-out base-colour texture";
    const std::vector<unsigned char> &png = loader.getTextureImages()[0];
    ASSERT_GE(png.size(), 8U);
    const unsigned char png_magic[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    for (size_t i = 0; i < 8; ++i) { EXPECT_EQ(png[i], png_magic[i]) << "PNG signature byte " << i; }
}

TEST(GltfParseUnit, HighContrastMaskCardExtractsItsTexture)
{
    // Bisecting the 1c-PT blocker (an addModel'd mask_card_hc rendered SOLID WHITE
    // in path tracing, i.e. textureID -1 / diffuse fallback). The PNG is a verified
    // valid RGBA (black + alpha checkerboard). This checks the CPU HALF: does the
    // loader EXTRACT the texture and set textureID? If yes, the bug is engine
    // upload/render side; if no, it is extraction. Device-free.
    const auto path = sceneConfig::resolveModelPath("Models/GltfTest/mask_card_hc.gltf");
    if (!std::filesystem::exists(path)) { GTEST_SKIP() << "mask_card_hc fixture not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(path));
    ASSERT_GT(loader.getMaterials().size(), 0U);

    EXPECT_NEAR(loader.getMaterials()[0].alphaCutoff, 0.5F, 1e-6F) << "MASK cutoff must survive";
    EXPECT_GE(loader.getMaterials()[0].get_textureID(), 0)
      << "the base-colour texture must extract and set a valid textureID (else the card "
         "samples the diffuse fallback - the white-in-PT symptom)";
    EXPECT_EQ(loader.getTextureImages().size(), 1U) << "exactly one extracted base-colour image";
}

TEST(GltfParseUnit, ReadsKhrTextureTransformScale)
{
    // glTF KHR_texture_transform scales/offsets the base-colour UV. The loader
    // used to ignore it entirely, so an atlas/tiled material sampled at the raw
    // UV. uv_transform_card.gltf carries scale [4,4] on its base-colour texture.
    // Red without the GltfLoader change: the rows stay the constructor's
    // identity (1,0,0)/(0,1,0) and the texture would not tile.
    const auto path = sceneConfig::resolveModelPath("Models/GltfTest/uv_transform_card.gltf");
    if (!std::filesystem::exists(path)) { GTEST_SKIP() << "uv_transform fixture not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(path));
    ASSERT_GT(loader.getMaterials().size(), 0U);

    const ObjMaterial &material = loader.getMaterials()[0];
    EXPECT_NEAR(material.uv_transform_row0.x, 4.0F, 1e-6F) << "KHR_texture_transform scale.x must reach ObjMaterial";
    EXPECT_NEAR(material.uv_transform_row0.y, 0.0F, 1e-6F) << "no rotation -> row0.y stays 0";
    EXPECT_NEAR(material.uv_transform_row0.z, 0.0F, 1e-6F) << "absent offset.x defaults to 0";
    EXPECT_NEAR(material.uv_transform_row1.x, 0.0F, 1e-6F) << "no rotation -> row1.x stays 0";
    EXPECT_NEAR(material.uv_transform_row1.y, 4.0F, 1e-6F) << "KHR_texture_transform scale.y must reach ObjMaterial";
    EXPECT_NEAR(material.uv_transform_row1.z, 0.0F, 1e-6F) << "absent offset.y defaults to 0";
}

TEST(GltfParseUnit, KhrTextureTransformRotationReachesTheMaterial)
{
    // uv_transform_rotation_card.gltf carries a +pi/2 rotation (no scale/offset)
    // on its base-colour texture. Assert the resulting rows transform a known UV
    // to the same point the spec's T*R*S formula gives, AND pin the sign against
    // the Rust loader's convention (gltf_loader.rs:618-625: rotate(-rotation)) -
    // a silently flipped sign produces a plausible-looking but mirrored image
    // that no other test would catch.
    const auto path = sceneConfig::resolveModelPath("Models/GltfTest/uv_transform_rotation_card.gltf");
    if (!std::filesystem::exists(path)) { GTEST_SKIP() << "uv_transform_rotation fixture not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(path));
    ASSERT_GT(loader.getMaterials().size(), 0U);

    const ObjMaterial &material = loader.getMaterials()[0];
    const float rotation = 1.5707963267948966F;// +pi/2, matches the fixture

    // Spec formula (KHR_texture_transform, T*R*S with the spec's rotation
    // convention baked in as "rotate by -rotation"), applied directly to a UV -
    // independent re-derivation, not just re-reading the loader's own output.
    const float cosR = std::cos(-rotation);
    const float sinR = std::sin(-rotation);
    const glm::vec2 uv(1.0F, 0.0F);
    const glm::vec2 expected(cosR * uv.x - sinR * uv.y, sinR * uv.x + cosR * uv.y);

    const glm::vec2 actual(glm::dot(glm::vec3(uv, 1.0F), material.uv_transform_row0),
      glm::dot(glm::vec3(uv, 1.0F), material.uv_transform_row1));
    EXPECT_NEAR(actual.x, expected.x, 1e-5F) << "rotated UV.x must match the spec T*R*S formula";
    EXPECT_NEAR(actual.y, expected.y, 1e-5F) << "rotated UV.y must match the spec T*R*S formula";

    // Sign pin: a +pi/2 rotation applied to (1,0) must land where
    // Mat3::from_angle(-pi/2) puts it (the Rust convention), i.e. (0,-1) -
    // NOT (0,1), which is what an un-negated rotation would give.
    EXPECT_NEAR(actual.x, 0.0F, 1e-5F) << "sign pin vs. the Rust loader's rotate(-rotation) convention";
    EXPECT_NEAR(actual.y, -1.0F, 1e-5F) << "sign pin vs. the Rust loader's rotate(-rotation) convention";
}

TEST(GltfParseUnit, MaterialWithoutTextureTransformIsIdentity)
{
    // A material with no KHR_texture_transform must default to identity rows
    // (1,0,0)/(0,1,0) so its texture samples exactly as before the extension
    // existed.
    const auto path = sceneConfig::resolveModelPath("Models/GltfTest/mask_card.gltf");
    if (!std::filesystem::exists(path)) { GTEST_SKIP() << "mask_card fixture not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(path));
    ASSERT_GT(loader.getMaterials().size(), 0U);

    const ObjMaterial &material = loader.getMaterials()[0];
    EXPECT_NEAR(material.uv_transform_row0.x, 1.0F, 1e-6F);
    EXPECT_NEAR(material.uv_transform_row0.y, 0.0F, 1e-6F);
    EXPECT_NEAR(material.uv_transform_row0.z, 0.0F, 1e-6F);
    EXPECT_NEAR(material.uv_transform_row1.x, 0.0F, 1e-6F);
    EXPECT_NEAR(material.uv_transform_row1.y, 1.0F, 1e-6F);
    EXPECT_NEAR(material.uv_transform_row1.z, 0.0F, 1e-6F);
}

TEST(GltfParseUnit, MultiPrimitiveGltfRecordsPerPrimitiveMeshRanges)
{
    // two_primitives.gltf is ONE mesh with TWO primitives (two materials). The
    // multi-mesh loader split (backlog #10) builds one Mesh per primitive:
    // parseCpu records one MeshRange per primitive slicing the flat arrays, and
    // uploadParsed builds a Mesh from each. This proves the range recording (the
    // render half - one BLAS geometry per mesh - is a GPU concern). Red without
    // the split: getMeshRanges() is empty.
    const auto path = sceneConfig::resolveModelPath("Models/GltfTest/two_primitives.gltf");
    if (!std::filesystem::exists(path)) { GTEST_SKIP() << "two-primitive fixture not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(path));

    ASSERT_EQ(loader.getMeshRanges().size(), 2U) << "one MeshRange per glTF primitive";
    EXPECT_EQ(loader.getVertices().size(), 8U) << "two quads = eight vertices";
    EXPECT_EQ(loader.getIndices().size(), 12U) << "two quads = four triangles";

    const auto &r0 = loader.getMeshRanges()[0];
    const auto &r1 = loader.getMeshRanges()[1];
    EXPECT_EQ(r0.vertexBase, 0U);
    EXPECT_EQ(r0.vertexCount, 4U);
    EXPECT_EQ(r1.vertexBase, 4U) << "the second primitive's vertices follow the first";
    EXPECT_EQ(r1.vertexCount, 4U);
    // The ranges partition the flat arrays exactly.
    EXPECT_EQ(r0.indexCount + r1.indexCount, loader.getIndices().size());
    EXPECT_EQ(r0.triCount + r1.triCount, loader.getMaterialIndices().size());
    // Each primitive keeps its own material (materials 0 and 1).
    ASSERT_GT(loader.getMaterialIndices().size(), r1.triStart);
    EXPECT_EQ(loader.getMaterialIndices()[r0.triStart], 0U);
    EXPECT_EQ(loader.getMaterialIndices()[r1.triStart], 1U);
}

TEST(GltfParseUnit, ReparsingTheSameLoaderDoesNotAccumulateMeshRanges)
{
    // GltfLoader::parseCpu did not clear meshRanges, unlike the other five
    // per-parse arrays (and unlike ObjLoader, which clears all six). Calling
    // parseCpu twice on one instance would append the second parse's ranges
    // to the first's. Red without the fix: getMeshRanges().size() doubles.
    const auto path = sceneConfig::resolveModelPath("Models/GltfTest/two_primitives.gltf");
    if (!std::filesystem::exists(path)) { GTEST_SKIP() << "two-primitive fixture not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(path));
    const std::size_t firstCount = loader.getMeshRanges().size();

    ASSERT_TRUE(loader.parseCpu(path));
    EXPECT_EQ(loader.getMeshRanges().size(), firstCount) << "reparsing must not accumulate mesh ranges";

    const auto &last = loader.getMeshRanges().back();
    EXPECT_EQ(last.indexStart + last.indexCount, loader.getIndices().size())
      << "the last range must still end exactly at the flat index array's end";
}

TEST(GltfParseUnit, ReadsColor0VertexColours)
{
    // vertex_colored_quad.gltf tags its four corners red/green/blue/white via
    // COLOR_0. The loader used to hardcode (1,1,1), so vertex-coloured glTF
    // rendered white and the forwarded fragment_color was dead. Red without the
    // loader change: every colour would come back (1,1,1).
    const auto path = sceneConfig::resolveModelPath("Models/GltfTest/vertex_colored_quad.gltf");
    if (!std::filesystem::exists(path)) { GTEST_SKIP() << "vertex-colour fixture not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(path));
    ASSERT_EQ(loader.getVertices().size(), 4U);

    const auto has_color = [&](float r, float g, float b) {
        for (const Vertex &v : loader.getVertices()) {
            if (std::abs(v.color.x - r) < 1e-4F && std::abs(v.color.y - g) < 1e-4F
                && std::abs(v.color.z - b) < 1e-4F) {
                return true;
            }
        }
        return false;
    };
    EXPECT_TRUE(has_color(1.0F, 0.0F, 0.0F)) << "red corner colour missing";
    EXPECT_TRUE(has_color(0.0F, 1.0F, 0.0F)) << "green corner colour missing";
    EXPECT_TRUE(has_color(0.0F, 0.0F, 1.0F)) << "blue corner colour missing";
    EXPECT_TRUE(has_color(1.0F, 1.0F, 1.0F)) << "white corner colour missing";
}

TEST(GltfParseUnit, VertexColorAlphaIsCarriedFromColor0)
{
    // COLOR_0's alpha is the third factor of the glTF MASK alpha product
    // (baseColorFactor.a * baseColorTexture.a * COLOR_0.a) - Vertex::color
    // used to be a vec3, so this component was silently dropped between
    // GltfLoader and the vertex buffer. Two-triangle strip (4 vertices, mode
    // 5, matching TriangleStripIsTriangulatedNotDropped's POSITION fixture)
    // whose VEC4 COLOR_0 alpha is 0.25 everywhere.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0, "COLOR_0": 1 },
        "mode": 5
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [
        { "componentType": 5126, "count": 4, "type": "VEC3",
          "min": [0,0,0], "max": [1,1,0], "bufferView": 0 },
        { "componentType": 5126, "count": 4, "type": "VEC4", "bufferView": 1 }
      ],
      "bufferViews": [
        { "buffer": 0, "byteLength": 48 },
        { "buffer": 1, "byteLength": 64 }
      ],
      "buffers": [
        { "byteLength": 48,
          "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAACAPwAAgD8AAAAA" },
        { "byteLength": 64,
          "uri": "data:application/octet-stream;base64,AACAPwAAgD8AAIA/AACAPgAAgD8AAIA/AACAPwAAgD4AAIA/AACAPwAAgD8AAIA+AACAPwAAgD8AAIA/AACAPg==" }
      ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_color0_vec4.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getVertices().size(), 4U);
    for (const Vertex &v : loader.getVertices()) {
        EXPECT_NEAR(v.color.w, 0.25F, 1e-4F) << "VEC4 COLOR_0 alpha must reach Vertex::color's fourth component";
    }
}

TEST(GltfParseUnit, Vec3Color0DefaultsVertexAlphaToOne)
{
    // A VEC3 COLOR_0 accessor has no alpha component. cgltf_accessor_read_float
    // only writes the accessor's own component count, so reading it into a
    // vec4 with N=4 would otherwise leave the fourth component untouched -
    // this pins the loader's pre-fill (readAttribute<4>'s VecT(1.0F) default)
    // rather than uninitialized/garbage alpha.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0, "COLOR_0": 1 },
        "mode": 5
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [
        { "componentType": 5126, "count": 4, "type": "VEC3",
          "min": [0,0,0], "max": [1,1,0], "bufferView": 0 },
        { "componentType": 5126, "count": 4, "type": "VEC3", "bufferView": 1 }
      ],
      "bufferViews": [
        { "buffer": 0, "byteLength": 48 },
        { "buffer": 1, "byteLength": 48 }
      ],
      "buffers": [
        { "byteLength": 48,
          "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAACAPwAAgD8AAAAA" },
        { "byteLength": 48,
          "uri": "data:application/octet-stream;base64,AAAAP5qZGT8zMzM/AAAAP5qZGT8zMzM/AAAAP5qZGT8zMzM/AAAAP5qZGT8zMzM/" }
      ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_color0_vec3.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getVertices().size(), 4U);
    for (const Vertex &v : loader.getVertices()) {
        EXPECT_NEAR(v.color.w, 1.0F, 1e-6F)
          << "VEC3 COLOR_0 must default Vertex::color's alpha to 1.0, not leave it uninitialized";
        EXPECT_NEAR(v.color.x, 0.5F, 1e-4F) << "VEC3 COLOR_0's rgb must still reach Vertex::color";
    }
}

TEST(GltfParseUnit, CorruptEmbeddedImageStillAssignsDenseTextureSlots)
{
    // corrupt_embedded_image.gltf has two materials, each with its own
    // base-colour texture: image 0 is a base64 blob that decodes to bytes (so
    // extractImageBytes succeeds) but is not a valid PNG/JPG, so
    // Texture::createFromMemory will fail to DECODE it later in uploadParsed
    // (device-side, not covered here); image 1 is a small valid PNG. This is
    // the CPU half of the 2026-07-23 texture-index-misalignment fix
    // (uploadParsed fills a failed decode with the default texture instead of
    // skipping the slot): parseCpu itself must still record BOTH images and
    // point each material at its OWN dense textureID, regardless of whether
    // either image will later decode. Red without the fix path mattering here
    // would be a parseCpu that skips extraction on decode failure - it does
    // not, since decoding only happens in uploadParsed - but this pins the
    // input the device-side fix depends on: two extracted images, materials
    // 0 and 1 pointing at slots 0 and 1 respectively.
    const auto path = sceneConfig::resolveModelPath("Models/GltfTest/corrupt_embedded_image.gltf");
    if (!std::filesystem::exists(path)) { GTEST_SKIP() << "corrupt-embedded-image fixture not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(path));

    ASSERT_EQ(loader.getTextureImages().size(), 2U) << "both images must be extracted, corrupt or not";
    ASSERT_GE(loader.getMaterials().size(), 2U) << "the two declared materials, plus the neutral fallback";
    EXPECT_EQ(loader.getMaterials()[0].textureID, 0) << "material 0 must reference its own texture slot";
    EXPECT_EQ(loader.getMaterials()[1].textureID, 1)
      << "material 1 must reference slot 1, not be shifted down by material 0's eventual decode failure";
}

TEST(GltfParseUnit, PrimitiveWithoutMaterialRoutesToNeutralFallback)
{
    // A primitive whose "material" key is absent (primitive->material == nullptr)
    // must route to a trailing neutral material so every face id is in range. The
    // OBJ twin (FacesWithoutAMaterialIndexInsideTheMaterialsArray) once caught a
    // real OOB there; this is the symmetric glTF guard, previously uncovered.
    //
    // The document has NO materials array and the primitive has NO material key,
    // so the loader's fallback must supply the only entry.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 }
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_no_material.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()))
      << "a valid glTF with no material on the primitive must parse successfully";
    std::filesystem::remove(tmp);

    // The loader appends one neutral fallback material for exactly this case.
    ASSERT_EQ(loader.getMaterials().size(), 1U)
      << "no declared materials + one material-less primitive = exactly the fallback";
    ASSERT_EQ(loader.getIndices().size() % 3U, 0U)
      << "indices must form whole triangles";
    ASSERT_EQ(loader.getMaterialIndices().size(), loader.getIndices().size() / 3U)
      << "materialIndex is one id per triangle";

    // Every face must point at the single fallback material (index 0).
    for (size_t i = 0; i < loader.getMaterialIndices().size(); ++i) {
        const unsigned int id = loader.getMaterialIndices()[i];
        EXPECT_LT(id, loader.getMaterials().size())
          << "face material index escapes the materials array (GPU OOB read) at triangle " << i;
        EXPECT_EQ(id, 0U)
          << "a material-less primitive must use the neutral fallback (index 0), not an arbitrary material";
    }
}

TEST(GltfParseUnit, OnlyTheDefaultSceneIsLoaded)
{
    // Two scenes, each a single-triangle mesh node at a distinct translation.
    // "scene": 1 names scene 1 as the default - the Rust loader's rule this
    // pins. A loader that (incorrectly) still merges every node in the
    // document would load both triangles (6 vertices); one that picks scene 0
    // instead of the named default would load the untranslated triangle.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "scene": 1,
      "scenes": [ { "nodes": [ 0 ] }, { "nodes": [ 1 ] } ],
      "nodes": [
        { "mesh": 0, "translation": [0, 0, 0] },
        { "mesh": 0, "translation": [100, 0, 0] }
      ],
      "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_default_scene.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }
    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getVertices().size(), 3U) << "only scene 1's single triangle must load";
    for (const Vertex &v : loader.getVertices()) {
        EXPECT_GT(v.position.x, 99.0F) << "the loaded triangle must carry scene 1's +100 translation";
    }
}

TEST(GltfParseUnit, NonZeroBaseColourTexCoordSetIsReported)
{
    // fromGltfMaterial's TEXCOORD_1/... diagnostic (increment: warn instead of
    // silently sampling UV0) hinges on Kataglyphis::describeTexCoordSet - this
    // pins that helper directly, no device or Vulkan needed. Set 0 is the only
    // set Vertex can carry, so it alone is "supported"; anything else is not.
    const Kataglyphis::TexCoordSetInfo set0 = Kataglyphis::describeTexCoordSet(0);
    EXPECT_EQ(set0.set, 0U);
    EXPECT_TRUE(set0.supported) << "TEXCOORD_0 is the one UV set Vertex carries";

    const Kataglyphis::TexCoordSetInfo set1 = Kataglyphis::describeTexCoordSet(1);
    EXPECT_EQ(set1.set, 1U);
    EXPECT_FALSE(set1.supported) << "TEXCOORD_1 has no Vertex slot to land in";
}

TEST(GltfParseUnit, ANodeNoSceneReferencesIsNotLoaded)
{
    // One scene lists node 0; node 1 is an orphan with its own mesh that no
    // scene references. Only node 0's geometry must arrive.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "scene": 0,
      "scenes": [ { "nodes": [ 0 ] } ],
      "nodes": [
        { "mesh": 0, "translation": [0, 0, 0] },
        { "mesh": 0, "translation": [100, 0, 0] }
      ],
      "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_orphan_node.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }
    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getVertices().size(), 3U) << "the orphan node's mesh must not load";
    for (const Vertex &v : loader.getVertices()) {
        EXPECT_LT(v.position.x, 2.0F) << "only the scene-referenced node's (untranslated) triangle must load";
    }
}

TEST(GltfParseUnit, ChildNodesOfASceneRootAreStillLoaded)
{
    // The scene lists only the root (node 0, meshless); its mesh-bearing child
    // (node 1) must still be reached by the recursion, or the fix would
    // regress from "every node in the document" to "roots only".
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "scene": 0,
      "scenes": [ { "nodes": [ 0 ] } ],
      "nodes": [
        { "children": [ 1 ] },
        { "mesh": 0, "translation": [50, 0, 0] }
      ],
      "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
      "accessors": [ { "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0,0,0], "max": [1,1,0], "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
      "buffers": [ { "byteLength": 36,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_scene_root_child.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }
    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string())) << "a scene root's mesh-bearing child must still be loaded";
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getVertices().size(), 3U);
    for (const Vertex &v : loader.getVertices()) {
        EXPECT_GT(v.position.x, 49.0F) << "the child node's own translation must apply";
    }
}

TEST(GltfParseUnit, AuthoredTangentsArePreferredOverGeneratedOnes)
{
    // A document shipping a TANGENT accessor must keep those values verbatim
    // instead of running vertex::computeTangents over them. The node carries
    // no transform (identity, unmirrored), so the authored (1,0,0,1) must
    // survive parseCpu byte-for-byte.
    const char *doc = R"GLTF({
      "asset": { "version": "2.0" },
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0, "TANGENT": 1 }
      } ] } ],
      "nodes": [ { "mesh": 0 } ],
      "scenes": [ { "nodes": [ 0 ] } ],
      "accessors": [
        { "componentType": 5126, "count": 3, "type": "VEC3",
          "min": [0,0,0], "max": [1,1,0], "bufferView": 0 },
        { "componentType": 5126, "count": 3, "type": "VEC4", "bufferView": 1 }
      ],
      "bufferViews": [
        { "buffer": 0, "byteOffset": 0, "byteLength": 36 },
        { "buffer": 1, "byteOffset": 0, "byteLength": 48 }
      ],
      "buffers": [
        { "byteLength": 36,
          "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" },
        { "byteLength": 48,
          "uri": "data:application/octet-stream;base64,AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAIA/AAAAAAAAAAAAAIA/" }
      ]
    })GLTF";
    const auto tmp = std::filesystem::temp_directory_path() / "kat_authored_tangent.gltf";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << doc;
    }
    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));
    std::filesystem::remove(tmp);

    ASSERT_EQ(loader.getVertices().size(), 3U);
    for (const Vertex &v : loader.getVertices()) {
        EXPECT_NEAR(v.tangent.x, 1.0F, 1e-5F);
        EXPECT_NEAR(v.tangent.y, 0.0F, 1e-5F);
        EXPECT_NEAR(v.tangent.z, 0.0F, 1e-5F);
        EXPECT_NEAR(v.tangent.w, 1.0F, 1e-5F) << "authored handedness must survive unmirrored/untransformed";
    }
}

TEST(GltfParseUnit, MissingTangentsAreGeneratedWithUnitLengthAndOrthogonalToTheNormal)
{
    // No TANGENT attribute: vertex::computeTangents must run and produce a
    // tangent that is unit length and perpendicular to the (flat, computed)
    // normal for every vertex of a real asset, not just a hand-built triangle.
    if (!std::filesystem::exists(test_gltf())) { GTEST_SKIP() << "test glb not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(test_gltf()));

    ASSERT_GT(loader.getVertices().size(), 0U);
    for (const Vertex &v : loader.getVertices()) {
        const float tangentLen = glm::length(glm::vec3(v.tangent));
        EXPECT_NEAR(tangentLen, 1.0F, 1e-3F) << "generated tangent must be unit length";
        EXPECT_NEAR(glm::dot(glm::vec3(v.tangent), v.normal), 0.0F, 1e-3F)
          << "generated tangent must be orthogonal to the vertex normal";
        EXPECT_TRUE(v.tangent.w == 1.0F || v.tangent.w == -1.0F) << "handedness must be exactly +-1";
    }
}
