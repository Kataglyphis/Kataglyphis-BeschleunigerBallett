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

#include <cstddef>
#include <filesystem>
#include <fstream>
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

// The async wrapper (AsyncModelParse) has its own dedicated coverage in
// asyncModelParseSuite.cpp, suite AsyncModelParseUnit.
