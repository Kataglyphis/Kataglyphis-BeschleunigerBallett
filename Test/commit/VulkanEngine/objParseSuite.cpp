// The CPU half of model loading, exercised with NO Vulkan device.
//
// This is the whole point of ObjLoader::parseCpu: 2802 ms of a 2818 ms model
// load is device-free work (measured on the bundled 27 MB dinosaurs.obj), so
// it can move to a worker thread while the ~15 ms of GPU upload stays on the
// thread that owns the device.
//
// A test that needed a device could not prove that. These construct
// ObjLoader{} - the CPU-only constructor - and would fail to link or crash if
// the parse path touched Vulkan.

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <memory>
#include <thread>

import kataglyphis.vulkan.obj_loader;
import kataglyphis.vulkan.scene_config;
import kataglyphis.vulkan.scene;

namespace {

std::string test_model()
{
    // The purpose-built shadow rig: small, committed, and known-good.
    return sceneConfig::resolveModelPath("Models/ShadowTest/shadow_rig.obj");
}

}// namespace

TEST(ObjParseUnit, ParsesWithoutAVulkanDevice)
{
    if (!std::filesystem::exists(test_model())) { GTEST_SKIP() << "test model not present"; }

    Kataglyphis::ObjLoader loader;// no device, no queue, no command pool
    ASSERT_TRUE(loader.parseCpu(test_model()));

    EXPECT_GT(loader.getVertices().size(), 0U);
    EXPECT_GT(loader.getIndices().size(), 0U);
    EXPECT_EQ(loader.getIndices().size() % 3U, 0U) << "indices must form whole triangles";
}

TEST(ObjParseUnit, FacesWithoutAMaterialIndexInsideTheMaterialsArray)
{
    if (!std::filesystem::exists(test_model())) { GTEST_SKIP() << "test model not present"; }

    // shadow_rig.obj ships no mtllib, so tinyobj reports material_id -1 for
    // every face. The old plain uint32_t cast sent 0xFFFFFFFF to the GPU and
    // every shader material fetch (materials.m[materialIDs.i[prim]]) became
    // an out-of-bounds buffer-device-address read - on EVERY untextured
    // model, silently, for the whole life of the loader. The loader appends
    // a default material for exactly this case; the faces must actually
    // point at it.
    Kataglyphis::ObjLoader loader;
    ASSERT_TRUE(loader.parseCpu(test_model()));

    ASSERT_FALSE(loader.getMaterials().empty())
      << "a file without materials must still yield the default material";
    for (const unsigned int index : loader.getMaterialIndices()) {
        ASSERT_LT(index, loader.getMaterials().size())
          << "face material index escapes the materials array (GPU OOB read)";
    }
}

TEST(ObjParseUnit, AnObjWithoutAnMtlGetsANonEmittingMaterial)
{
    // emission(0, 0, 0.10) was inert until shading started reading it - it
    // then became a constant blue glow on every .obj shipped without an
    // .mtl. No authored Ke means no emitted radiance.
    const auto tmp = std::filesystem::temp_directory_path() / "kat_no_mtllib.obj";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }

    Kataglyphis::ObjLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));

    ASSERT_EQ(loader.getMaterials().size(), 1U);
    const glm::vec3 &emission = loader.getMaterials().front().emission;
    EXPECT_FLOAT_EQ(emission.x, 0.0F);
    EXPECT_FLOAT_EQ(emission.y, 0.0F);
    EXPECT_FLOAT_EQ(emission.z, 0.0F);

    std::filesystem::remove(tmp);
}

TEST(ObjParseUnit, UntexturedMtlMaterialsRouteToTheDiffuseFallback)
{
    const std::string dino = sceneConfig::resolveModelPath("Models/Dinosaurs/dinosaurs.obj");
    if (!std::filesystem::exists(dino)) { GTEST_SKIP() << "test model not present"; }

    // dinosaurs.mtl carries Kd colours but not a single map_Kd. The loader
    // used to give those materials textureID = 0, so they sampled texture
    // slot 0 (the default white) and the model rendered its material colours
    // as flat white for the engine's whole life. -1 is the contract the
    // shaders' diffuse fallback keys on.
    Kataglyphis::ObjLoader loader;
    ASSERT_TRUE(loader.parseCpu(dino));

    ASSERT_FALSE(loader.getMaterials().empty());
    for (const auto &material : loader.getMaterials()) {
        EXPECT_EQ(material.get_textureID(), -1)
          << "a material without map_Kd must route to the diffuse fallback";
    }
}

TEST(ObjParseUnit, MaterialsHaveZeroMetallic)
{
    const std::string dino = sceneConfig::resolveModelPath("Models/Dinosaurs/dinosaurs.obj");
    if (!std::filesystem::exists(dino)) { GTEST_SKIP() << "test model not present"; }

    // Wavefront .mtl has no metallic channel, so every OBJ material must come
    // back with ObjMaterial's default metallic (0.0) - OBJ scenes stay
    // bit-identical to their pre-metallic-field rendering.
    Kataglyphis::ObjLoader loader;
    ASSERT_TRUE(loader.parseCpu(dino));

    ASSERT_FALSE(loader.getMaterials().empty());
    for (const auto &material : loader.getMaterials()) {
        EXPECT_NEAR(material.metallic, 0.0F, 1e-6F) << "an OBJ material must default to metallic 0.0";
    }
}

TEST(ObjParseUnit, MultiShapeObjRecordsPerShapeMeshRanges)
{
    // The OBJ analog of the glTF primitive split (#10): each OBJ shape (`o`/`g`
    // group) becomes its own MeshRange so uploadParsed builds one Mesh per shape.
    // dinosaurs.obj carries three `o` groups. The flat getters are unchanged (the
    // tests above still hold) - the ranges just partition them. Red proof is
    // structural: without the per-shape recording in loadVertices, getMeshRanges()
    // is empty and the > 1 assertion fails.
    const std::string dino = sceneConfig::resolveModelPath("Models/Dinosaurs/dinosaurs.obj");
    if (!std::filesystem::exists(dino)) { GTEST_SKIP() << "test model not present"; }

    Kataglyphis::ObjLoader loader;
    ASSERT_TRUE(loader.parseCpu(dino));

    const auto &ranges = loader.getMeshRanges();
    EXPECT_GT(ranges.size(), 1U) << "a multi-shape OBJ must split into more than one mesh";

    // The ranges must tile the flat arrays contiguously with no gap or overlap:
    // uploadParsed slices [vertexBase, vertexBase+vertexCount) out of the flat
    // vertices and re-bases each shape's indices by -vertexBase, which is only
    // valid if a shape's indices never reach outside its own vertex block (the
    // per-shape dedup is what guarantees that).
    std::size_t vsum = 0;
    std::size_t isum = 0;
    std::size_t tsum = 0;
    for (const auto &r : ranges) {
        EXPECT_EQ(r.vertexBase, vsum) << "vertex blocks must be contiguous";
        EXPECT_EQ(r.indexStart, isum) << "index blocks must be contiguous";
        EXPECT_EQ(r.triStart, tsum) << "material-index blocks must be contiguous";
        EXPECT_GT(r.vertexCount, 0U);
        EXPECT_EQ(r.indexCount % 3U, 0U) << "each shape's indices form whole triangles";
        for (std::size_t i = 0; i < r.indexCount; ++i) {
            const unsigned int idx = loader.getIndices()[r.indexStart + i];
            EXPECT_GE(static_cast<std::size_t>(idx), r.vertexBase);
            EXPECT_LT(static_cast<std::size_t>(idx), r.vertexBase + r.vertexCount)
              << "a shape index escapes its own vertex block - the slice re-base would corrupt it";
        }
        vsum += r.vertexCount;
        isum += r.indexCount;
        tsum += r.triCount;
    }
    EXPECT_EQ(vsum, loader.getVertices().size()) << "ranges must cover every vertex";
    EXPECT_EQ(isum, loader.getIndices().size()) << "ranges must cover every index";
    EXPECT_EQ(tsum, loader.getMaterialIndices().size()) << "ranges must cover every face material";
}

TEST(ObjParseUnit, SingleShapeObjIsOneMeshRangeSpanningEverything)
{
    // The safe-by-default half: a single-object OBJ yields exactly one range over
    // the whole model, so uploadParsed builds the same single mesh it always did.
    if (!std::filesystem::exists(test_model())) { GTEST_SKIP() << "test model not present"; }

    Kataglyphis::ObjLoader loader;
    ASSERT_TRUE(loader.parseCpu(test_model()));

    const auto &ranges = loader.getMeshRanges();
    ASSERT_EQ(ranges.size(), 1U) << "shadow_rig.obj is a single shape -> one mesh, as before";
    EXPECT_EQ(ranges[0].vertexBase, 0U);
    EXPECT_EQ(ranges[0].vertexCount, loader.getVertices().size());
    EXPECT_EQ(ranges[0].indexStart, 0U);
    EXPECT_EQ(ranges[0].indexCount, loader.getIndices().size());
    EXPECT_EQ(ranges[0].triCount, loader.getMaterialIndices().size());
}

TEST(ModelPickerUnit, GltfModelsAppearInTheAvailableList)
{
    // The GUI picker's scan filtered on == ".obj" (case-sensitive), so the
    // bundled GltfTest/cube.glb could never be selected even though the
    // engine has a glTF loader wired into every other load path.
    const auto paths = sceneConfig::getAvailableModelPaths();
    if (paths.empty()) { GTEST_SKIP() << "no Resources/Models directory in this environment"; }

    const bool has_gltf = std::any_of(paths.begin(), paths.end(), [](const std::string &path) {
        auto lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return lower.ends_with(".glb") || lower.ends_with(".gltf");
    });
    EXPECT_TRUE(has_gltf) << "no glTF asset in the model picker list - the extension filter is OBJ-only again";
}

TEST(ModelPickerUnit, AddingANullModelIsSafeNotACrash)
{
    // Loaders return nullptr for malformed assets; Scene::add_model used to
    // dereference it unconditionally (model->getObjectDescription()), so a
    // bad file picked in the GUI crashed the app inside reloadModel.
    Kataglyphis::Scene scene;
    scene.add_model(nullptr);
    EXPECT_EQ(scene.getModelCount(), 0U) << "a failed load must leave the scene unchanged";
}

TEST(ObjParseUnit, MalformedInputFailsInsteadOfKillingTheProcess)
{
    // The GUI model picker can hand this arbitrary files. Until 2026-07-20
    // loadTexturesAndMaterials called exit(EXIT_FAILURE) here, so selecting a
    // bad file terminated the application - this test is the guard against
    // that returning.
    Kataglyphis::ObjLoader loader;

    EXPECT_FALSE(loader.parseCpu("this/path/does/not/exist.obj"));
    EXPECT_TRUE(loader.getVertices().empty()) << "a failed parse must leave no partial geometry";
}

TEST(ObjParseUnit, ReparsingReplacesRatherThanAppends)
{
    if (!std::filesystem::exists(test_model())) { GTEST_SKIP() << "test model not present"; }

    Kataglyphis::ObjLoader loader;
    ASSERT_TRUE(loader.parseCpu(test_model()));
    const size_t first_vertices = loader.getVertices().size();
    const size_t first_indices = loader.getIndices().size();

    ASSERT_TRUE(loader.parseCpu(test_model()));

    // Model reload reuses a loader. Accumulating would double the mesh on
    // every reload, which looks like z-fighting rather than like a leak.
    EXPECT_EQ(loader.getVertices().size(), first_vertices);
    EXPECT_EQ(loader.getIndices().size(), first_indices);
}

TEST(ObjParseUnit, ParsesOnAWorkerThreadWithTheSameResult)
{
    if (!std::filesystem::exists(test_model())) { GTEST_SKIP() << "test model not present"; }

    // The actual claim being made: this work can run off the render thread.
    // Asserting it here means the async loader cannot be built on a parse
    // that quietly depends on thread-local or device state.
    Kataglyphis::ObjLoader on_main;
    ASSERT_TRUE(on_main.parseCpu(test_model()));

    Kataglyphis::ObjLoader on_worker;
    bool worker_ok = false;
    std::thread worker([&] { worker_ok = on_worker.parseCpu(test_model()); });
    worker.join();

    ASSERT_TRUE(worker_ok);
    ASSERT_EQ(on_worker.getVertices().size(), on_main.getVertices().size());
    ASSERT_EQ(on_worker.getIndices().size(), on_main.getIndices().size());

    for (size_t i = 0; i < on_main.getIndices().size(); ++i) {
        ASSERT_EQ(on_worker.getIndices()[i], on_main.getIndices()[i]) << "index " << i << " differs off-thread";
    }
}

TEST(ObjParseUnit, AFaceWithAnOutOfRangeIndexIsDroppedWhole)
{
    // A malformed corner used to `continue` past just the bad vertex,
    // leaving the face one index short of a triangle - desynchronising
    // materialIndex from indices/3 and, via the flat-normal pass, reading
    // past the end of indices. validate-then-emit drops the whole face
    // instead: the good face survives untouched, the bad one leaves nothing.
    const auto tmp = std::filesystem::temp_directory_path() / "kat_bad_face.obj";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\nf 1 2 999999\n";
    }

    Kataglyphis::ObjLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));

    EXPECT_EQ(loader.getIndices().size() % 3U, 0U) << "indices must form whole triangles";
    EXPECT_EQ(loader.getIndices().size(), 3U) << "the good face survives, the bad one is gone entirely";
    EXPECT_EQ(loader.getMaterialIndices().size(), loader.getIndices().size() / 3U);

    std::filesystem::remove(tmp);
}

TEST(ObjParseUnit, AFileWithOnlyMalformedFacesYieldsNoGeometry)
{
    // Same hazard with nothing left standing: every face falls to the guard,
    // so the parse must leave empty arrays rather than crash walking them.
    const auto tmp = std::filesystem::temp_directory_path() / "kat_only_bad_face.obj";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 999999\n";
    }

    Kataglyphis::ObjLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string())) << "a malformed face must not fail the parse itself";

    EXPECT_TRUE(loader.getIndices().empty()) << "a file with only malformed faces yields no geometry";
    EXPECT_TRUE(loader.getVertices().empty());
    EXPECT_TRUE(loader.getMaterialIndices().empty());

    std::filesystem::remove(tmp);
}

TEST(ObjParseUnit, DegenerateTrianglesDoNotProduceNaNNormals)
{
    // No `vn` lines, so the loader falls back to flat-normal generation
    // (kataglyphis.vulkan.vertex::computeFlatNormals - the copy shared with
    // GltfLoader since this task consolidated the two hand-rolled loops).
    // The triangle is zero-area (two coincident vertices): normalizing its
    // cross product would divide by zero and hand the GPU a NaN normal.
    const auto tmp = std::filesystem::temp_directory_path() / "kat_degenerate_face.obj";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << "v 0 0 0\nv 0 0 0\nv 1 0 0\nf 1 2 3\n";
    }

    Kataglyphis::ObjLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));

    ASSERT_FALSE(loader.getVertices().empty());
    for (const auto &vertex : loader.getVertices()) {
        EXPECT_TRUE(std::isfinite(vertex.normal.x));
        EXPECT_TRUE(std::isfinite(vertex.normal.y));
        EXPECT_TRUE(std::isfinite(vertex.normal.z));
    }

    std::filesystem::remove(tmp);
}

TEST(ObjParseUnit, FacesWithoutANormalIndexGetAFlatNormal)
{
    // Only the second face omits `vn`. The old guard only ran
    // computeFlatNormals when attrib.normals was empty for the WHOLE file, so
    // a mixed file left the second face's vertices at the zero normal they
    // were initialised to. fillMissingFlatNormals must patch exactly that gap.
    const auto tmp = std::filesystem::temp_directory_path() / "kat_mixed_normals.obj";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
               "v 0 0 1\nv 1 0 1\nv 0 1 1\n"
               "vn 0 0 1\n"
               "f 1//1 2//1 3//1\n"
               "f 4 5 6\n";
    }

    Kataglyphis::ObjLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));

    ASSERT_FALSE(loader.getVertices().empty());
    for (const auto &vertex : loader.getVertices()) {
        const float len2 = glm::dot(vertex.normal, vertex.normal);
        EXPECT_NEAR(len2, 1.0F, 1e-4F) << "every vertex must end up with a unit-length normal";
    }

    std::filesystem::remove(tmp);
}

TEST(ObjParseUnit, FullyNormalLessObjStillGetsFlatNormals)
{
    // Behaviour-preservation check: a file with no `vn` at all must produce
    // the same result as before this task's guard rewrite.
    const auto tmp = std::filesystem::temp_directory_path() / "kat_no_normals.obj";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }

    Kataglyphis::ObjLoader loader;
    ASSERT_TRUE(loader.parseCpu(tmp.string()));

    ASSERT_FALSE(loader.getVertices().empty());
    for (const auto &vertex : loader.getVertices()) {
        const float len2 = glm::dot(vertex.normal, vertex.normal);
        EXPECT_NEAR(len2, 1.0F, 1e-4F);
        EXPECT_NEAR(vertex.normal.z, 1.0F, 1e-4F) << "flat normal for this triangle points along +Z";
    }

    std::filesystem::remove(tmp);
}

TEST(ObjParseUnit, MtlRelativeTextureIsResolvedBesideTheMtl)
{
    // docs/model-loading.md's first candidate: map_Kd resolves relative to
    // the directory containing the .mtl, which is what the OBJ/MTL format
    // actually specifies.
    const auto dir = std::filesystem::temp_directory_path() / "kat_mtl_beside";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    {
        std::ofstream mtl(dir / "beside.mtl", std::ios::binary);
        mtl << "newmtl painted\nKd 1 1 1\nmap_Kd paint.png\n";
    }
    { std::ofstream texture(dir / "paint.png", std::ios::binary); }
    {
        std::ofstream obj(dir / "beside.obj", std::ios::binary);
        obj << "mtllib beside.mtl\n"
               "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
               "vt 0 0\nvt 1 0\nvt 0 1\n"
               "usemtl painted\nf 1/1 2/2 3/3\n";
    }

    Kataglyphis::ObjLoader loader;
    ASSERT_TRUE(loader.parseCpu((dir / "beside.obj").string()));

    ASSERT_FALSE(loader.getTextureNames().empty());
    EXPECT_TRUE(std::filesystem::exists(loader.getTextureNames()[0]))
      << "the resolved texture path must name a file that actually exists: "
      << loader.getTextureNames()[0];

    std::filesystem::remove_all(dir);
}

TEST(ObjParseUnit, MaterialsSharingAMapKdShareOneTextureSlot)
{
    // Two newmtl blocks naming the same map_Kd must upload that texture once:
    // the second material reuses the first's textures slot instead of
    // pushing a duplicate path.
    const auto dir = std::filesystem::temp_directory_path() / "kat_mtl_shared_texture";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    {
        std::ofstream mtl(dir / "shared.mtl", std::ios::binary);
        mtl << "newmtl first\nKd 1 1 1\nmap_Kd paint.png\n"
               "newmtl second\nKd 1 1 1\nmap_Kd paint.png\n";
    }
    { std::ofstream texture(dir / "paint.png", std::ios::binary); }
    {
        std::ofstream obj(dir / "shared.obj", std::ios::binary);
        obj << "mtllib shared.mtl\n"
               "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\n"
               "vt 0 0\nvt 1 0\nvt 0 1\n"
               "usemtl first\nf 1/1 2/2 3/3\n"
               "usemtl second\nf 1/1 2/2 4/3\n";
    }

    Kataglyphis::ObjLoader loader;
    ASSERT_TRUE(loader.parseCpu((dir / "shared.obj").string()));

    ASSERT_EQ(loader.getMaterials().size(), 2U) << "both declared materials must be present";
    ASSERT_EQ(loader.getTextureNames().size(), 2U);
    EXPECT_FALSE(loader.getTextureNames()[0].empty()) << "the first material's texture must be recorded";
    EXPECT_TRUE(loader.getTextureNames()[1].empty()) << "the second, duplicate texture must not be re-pushed";
    EXPECT_EQ(loader.getMaterials()[0].textureID, 0);
    EXPECT_EQ(loader.getMaterials()[1].textureID, 0) << "the second material must share slot 0, not get its own";

    std::filesystem::remove_all(dir);
}

TEST(ObjParseUnit, MtlTextureUnderATexturesSubdirectoryIsResolved)
{
    // docs/model-loading.md's second candidate: the layout every shipped OBJ
    // in this repo actually uses (crytek-sponza, Pillum, Sulo/WolfStahl,
    // VikingRoom) - a bare filename in map_Kd, with the file itself one
    // level down in textures/.
    const auto dir = std::filesystem::temp_directory_path() / "kat_mtl_textures_subdir";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "textures");

    {
        std::ofstream mtl(dir / "sub.mtl", std::ios::binary);
        mtl << "newmtl painted\nKd 1 1 1\nmap_Kd paint.png\n";
    }
    { std::ofstream texture(dir / "textures" / "paint.png", std::ios::binary); }
    {
        std::ofstream obj(dir / "sub.obj", std::ios::binary);
        obj << "mtllib sub.mtl\n"
               "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
               "vt 0 0\nvt 1 0\nvt 0 1\n"
               "usemtl painted\nf 1/1 2/2 3/3\n";
    }

    Kataglyphis::ObjLoader loader;
    ASSERT_TRUE(loader.parseCpu((dir / "sub.obj").string()));

    ASSERT_FALSE(loader.getTextureNames().empty());
    EXPECT_TRUE(std::filesystem::exists(loader.getTextureNames()[0]))
      << "the textures/-subdirectory candidate must resolve: " << loader.getTextureNames()[0];

    std::filesystem::remove_all(dir);
}

TEST(ObjParseUnit, BackslashMapKdResolvesLikeAForwardSlash)
{
    // docs/model-loading.md: a Windows-authored map_Kd value must resolve
    // the same as its forward-slash equivalent. viking_room.mtl's map_Kd is
    // a bare filename with the texture one level down in textures/, so
    // spelling the map_Kd as "textures\viking_room.png" exercises both the
    // normalisation and the beside-the-.mtl candidate in one shot.
    const std::string obj = sceneConfig::resolveModelPath("Models/VikingRoom/viking_room.obj");
    if (!std::filesystem::exists(obj)) { GTEST_SKIP() << "VikingRoom asset not present"; }
    const std::string dir = std::filesystem::path(obj).parent_path().string();

    const std::string backslash_result = Kataglyphis::resolveObjTexturePath(dir, "textures\\viking_room.png");
    const std::string forward_slash_result = Kataglyphis::resolveObjTexturePath(dir, "textures/viking_room.png");

    EXPECT_EQ(backslash_result, forward_slash_result);
    EXPECT_TRUE(std::filesystem::exists(backslash_result))
      << "the resolved path must name a file that actually exists: " << backslash_result;
}

TEST(ObjParseUnit, BareFilenameFallsBackToTheTexturesSubdirectory)
{
    // viking_room.mtl's actual map_Kd value: a bare filename, with the
    // texture living one level down in textures/ - the second candidate.
    const std::string obj = sceneConfig::resolveModelPath("Models/VikingRoom/viking_room.obj");
    if (!std::filesystem::exists(obj)) { GTEST_SKIP() << "VikingRoom asset not present"; }
    const std::string dir = std::filesystem::path(obj).parent_path().string();

    const std::string resolved = Kataglyphis::resolveObjTexturePath(dir, "viking_room.png");

    EXPECT_TRUE(std::filesystem::exists(resolved))
      << "a bare filename must fall back to the textures/ subdirectory: " << resolved;
}

TEST(ObjParseUnit, EmptyBaseDirStaysRelative)
{
    // An OBJ referenced by a bare filename (no directory component) yields
    // an empty base dir from getBaseDir(); that must not turn into a
    // filesystem-root path ("/viking_room.png").
    const std::string resolved = Kataglyphis::resolveObjTexturePath("", "viking_room.png");

    EXPECT_FALSE(resolved.starts_with('/')) << "an empty base dir must not resolve against the filesystem root: "
                                             << resolved;
    EXPECT_FALSE(std::filesystem::path(resolved).is_absolute());
}

// The async wrapper (AsyncModelParse) has its own dedicated coverage in
// asyncModelParseSuite.cpp, suite AsyncModelParseUnit.
