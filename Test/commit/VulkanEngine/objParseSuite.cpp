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

#include <filesystem>
#include <string>
#include <chrono>
#include <memory>
#include <thread>

import kataglyphis.vulkan.obj_loader;
import kataglyphis.vulkan.gltf_loader;
import kataglyphis.vulkan.async_model_parse;
import kataglyphis.vulkan.scene_config;

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

// The async wrapper. Still no device anywhere - the whole point is that the
// parse half runs off the render thread.

TEST(AsyncModelParseUnit, ParsesOffThreadAndHandsBackTheResult)
{
    if (!std::filesystem::exists(test_model())) { GTEST_SKIP() << "test model not present"; }

    Kataglyphis::ObjLoader reference;
    ASSERT_TRUE(reference.parseCpu(test_model()));

    Kataglyphis::AsyncModelParse parse;
    parse.start(test_model());

    // Poll the way a frame loop would, rather than joining immediately.
    for (int spin = 0; spin < 10000 && !parse.isFinished(); ++spin) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(parse.isFinished()) << "the parse never completed";
    ASSERT_TRUE(parse.wasSuccessful());

    const std::unique_ptr<Kataglyphis::ObjLoader> result = parse.takeResult();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->getVertices().size(), reference.getVertices().size());
    EXPECT_EQ(result->getIndices().size(), reference.getIndices().size());
}

TEST(AsyncModelParseUnit, RoutesGltfToTheGltfLoaderOffThread)
{
    // A .glb must dispatch to GltfLoader on the worker, not ObjLoader. Same
    // off-thread contract as the OBJ case; parsedGltf() tells the caller which
    // result to take.
    const std::string gltf = sceneConfig::resolveModelPath("Models/GltfTest/cube.glb");
    if (!std::filesystem::exists(gltf)) { GTEST_SKIP() << "test glb not present"; }

    Kataglyphis::GltfLoader reference;
    ASSERT_TRUE(reference.parseCpu(gltf));

    Kataglyphis::AsyncModelParse parse;
    parse.start(gltf);
    for (int spin = 0; spin < 10000 && !parse.isFinished(); ++spin) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(parse.isFinished()) << "the parse never completed";
    ASSERT_TRUE(parse.wasSuccessful());
    ASSERT_TRUE(parse.parsedGltf()) << "a .glb must route to the glTF loader, not ObjLoader";

    const std::unique_ptr<Kataglyphis::GltfLoader> result = parse.takeGltfResult();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->getVertices().size(), reference.getVertices().size());
    EXPECT_EQ(result->getIndices().size(), reference.getIndices().size());
}

TEST(AsyncModelParseUnit, AFailedParseReportsFailureRatherThanEmptyGeometry)
{
    Kataglyphis::AsyncModelParse parse;
    parse.start("no/such/model.obj");
    parse.waitForCompletion();

    EXPECT_TRUE(parse.isFinished());
    EXPECT_FALSE(parse.wasSuccessful());
    // A caller must be able to tell "failed" from "loaded an empty model",
    // or a bad path silently replaces the scene with nothing.
    EXPECT_EQ(parse.takeResult(), nullptr);
}

TEST(AsyncModelParseUnit, StartingASecondParseSupersedesTheFirst)
{
    if (!std::filesystem::exists(test_model())) { GTEST_SKIP() << "test model not present"; }

    // The GUI can pick a third model while the second is still loading.
    // Newest-wins is the documented behaviour; what must NOT happen is the
    // two workers writing into the same loader.
    Kataglyphis::AsyncModelParse parse;
    parse.start(test_model());
    parse.start(test_model());
    parse.waitForCompletion();

    ASSERT_TRUE(parse.wasSuccessful());
    const auto result = parse.takeResult();
    ASSERT_NE(result, nullptr);
    EXPECT_GT(result->getVertices().size(), 0U);
}

TEST(AsyncModelParseUnit, DestructionJoinsRatherThanDetaches)
{
    if (!std::filesystem::exists(test_model())) { GTEST_SKIP() << "test model not present"; }

    // A detached worker writing into a destroyed loader is a use-after-free
    // that surfaces as corrupted geometry, not a crash. Under ASAN - which
    // this suite builds with - that would be caught here.
    {
        Kataglyphis::AsyncModelParse parse;
        parse.start(test_model());
        // Deliberately no wait: the destructor must handle it.
    }
    SUCCEED() << "destructor returned without a dangling worker";
}

TEST(AsyncModelParseUnit, IdleInstancesAreSafeToQueryAndTake)
{
    Kataglyphis::AsyncModelParse parse;
    EXPECT_FALSE(parse.isRunning());
    EXPECT_FALSE(parse.isFinished());
    EXPECT_EQ(parse.takeResult(), nullptr);
    parse.waitForCompletion();// must not deadlock on a never-started parse
}
