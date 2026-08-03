// Scene::reloadModel used to leave a pending beginModelLoadAsync() parse
// running: a reload issued during the startup parse joined nobody, so the
// worker's result landed in the scene (via pollModelLoad, still driven by the
// frame loop) alongside the reloaded model instead of being discarded.
// cancelPendingModelLoad() closes that gap; these pin its own behaviour and
// that reloadModel() actually calls it.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

import kataglyphis.vulkan.scene;

using Kataglyphis::Scene;

TEST(SceneAsyncLoad, ReloadCancelsAPendingParse)
{
    Scene scene;

    scene.beginModelLoadAsync();
    ASSERT_TRUE(scene.isModelLoadPending());

    scene.cancelPendingModelLoad();
    EXPECT_FALSE(scene.isModelLoadPending());

    // Idempotent, like every other cleanUp() in this codebase.
    scene.cancelPendingModelLoad();
    EXPECT_FALSE(scene.isModelLoadPending());
}

namespace {
namespace fs = std::filesystem;

// Tests run with the repo root as working directory (gtest_discover_tests sets
// WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}), but be forgiving if that changes.
fs::path find_repo_root()
{
    fs::path candidate = fs::current_path();
    for (int depth = 0; depth < 6; ++depth) {
        if (fs::exists(candidate / "Resources" / "ShadersSlang")) { return candidate; }
        if (!candidate.has_parent_path()) { break; }
        candidate = candidate.parent_path();
    }
    return {};
}
}// namespace

// reloadModel() replaces the scene wholesale; if it does not first cancel a
// still-running beginModelLoadAsync() parse, the worker's eventual result
// still lands via pollModelLoad and the scene ends up holding both the
// reloaded model and the startup one.
TEST(BuildIntegrity, ReloadModelCancelsAPendingAsyncParse)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path scene_path = repo_root / "Src" / "GraphicsEngineVulkan" / "scene" / "Scene.cpp";
    std::ifstream file(scene_path);
    ASSERT_TRUE(static_cast<bool>(file)) << "missing " << scene_path.string();
    const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    const std::size_t signature_pos = contents.find("Scene::reloadModel(");
    ASSERT_NE(signature_pos, std::string::npos) << "Scene::reloadModel is no longer defined in " << scene_path.string();

    const std::size_t body_open = contents.find('{', signature_pos);
    ASSERT_NE(body_open, std::string::npos) << "could not locate the opening brace of Scene::reloadModel";

    int brace_depth = 0;
    std::size_t body_close = std::string::npos;
    for (std::size_t i = body_open; i < contents.size(); ++i) {
        if (contents[i] == '{') { ++brace_depth; }
        else if (contents[i] == '}') {
            --brace_depth;
            if (brace_depth == 0) {
                body_close = i;
                break;
            }
        }
    }
    ASSERT_NE(body_close, std::string::npos) << "could not brace-match the closing '}' of Scene::reloadModel";

    const std::string body = contents.substr(body_open, body_close - body_open + 1);
    EXPECT_TRUE(body.find("cancelPendingModelLoad") != std::string::npos)
      << "Scene::reloadModel no longer calls cancelPendingModelLoad() - a reload issued while "
         "beginModelLoadAsync() is still parsing would leave the worker's result to land later via "
         "pollModelLoad, leaving the scene holding both the reloaded model and the startup one";
}
