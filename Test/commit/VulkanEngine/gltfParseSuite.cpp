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

namespace {

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
