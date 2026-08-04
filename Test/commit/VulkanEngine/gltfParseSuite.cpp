// The CPU half of glTF loading, exercised with NO Vulkan device - the parallel
// of objParseSuite for GltfLoader::parseCpu. Both loaders emit the same
// Vertex/index/ObjMaterial/materialIndex arrays, so this asserts the shared
// invariants (whole triangles, per-face material ids) rather than a byte-exact
// decode. The asset is the Rust renderer's cube.glb, copied in - the point of
// the feature is that both renderers load the SAME glTF.

#include <gtest/gtest.h>

#include <cmath>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>
#include <filesystem>
#include <fstream>
#include <string>

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
    // Red without the GltfLoader change: uv_scale stays the constructor default
    // (1,1) and the texture would not tile.
    const auto path = sceneConfig::resolveModelPath("Models/GltfTest/uv_transform_card.gltf");
    if (!std::filesystem::exists(path)) { GTEST_SKIP() << "uv_transform fixture not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(path));
    ASSERT_GT(loader.getMaterials().size(), 0U);

    const ObjMaterial &material = loader.getMaterials()[0];
    EXPECT_NEAR(material.uv_scale.x, 4.0F, 1e-6F) << "KHR_texture_transform scale.x must reach ObjMaterial";
    EXPECT_NEAR(material.uv_scale.y, 4.0F, 1e-6F) << "KHR_texture_transform scale.y must reach ObjMaterial";
    EXPECT_NEAR(material.uv_offset.x, 0.0F, 1e-6F) << "absent offset defaults to 0";
    EXPECT_NEAR(material.uv_offset.y, 0.0F, 1e-6F) << "absent offset defaults to 0";
}

TEST(GltfParseUnit, MaterialWithoutTextureTransformIsIdentity)
{
    // A material with no KHR_texture_transform must default to identity (scale
    // 1, offset 0) so its texture samples exactly as before the extension existed.
    const auto path = sceneConfig::resolveModelPath("Models/GltfTest/mask_card.gltf");
    if (!std::filesystem::exists(path)) { GTEST_SKIP() << "mask_card fixture not present"; }

    Kataglyphis::GltfLoader loader;
    ASSERT_TRUE(loader.parseCpu(path));
    ASSERT_GT(loader.getMaterials().size(), 0U);

    const ObjMaterial &material = loader.getMaterials()[0];
    EXPECT_NEAR(material.uv_scale.x, 1.0F, 1e-6F);
    EXPECT_NEAR(material.uv_scale.y, 1.0F, 1e-6F);
    EXPECT_NEAR(material.uv_offset.x, 0.0F, 1e-6F);
    EXPECT_NEAR(material.uv_offset.y, 0.0F, 1e-6F);
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
