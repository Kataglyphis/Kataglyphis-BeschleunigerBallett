// Regression guards for build-system bugs that were expensive to diagnose.
//
// These are filesystem checks, not GPU tests: they run anywhere, including CI
// containers without an adapter.

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// Tests run with the repo root as working directory (gtest_discover_tests sets
// WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}), but be forgiving if that changes.
fs::path find_repo_root()
{
    fs::path candidate = fs::current_path();
    for (int depth = 0; depth < 6; ++depth) {
        if (fs::exists(candidate / "Resources" / "Shaders")) { return candidate; }
        if (!candidate.has_parent_path()) { break; }
        candidate = candidate.parent_path();
    }
    return {};
}

// mtime of the most recently edited shared include, or false if there are none.
bool newest_shared_include(const fs::path &shader_root, fs::file_time_type &out)
{
    std::error_code error;
    bool found = false;
    for (fs::recursive_directory_iterator it(shader_root, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        if (!it->is_regular_file(error) || it->path().extension() != ".glsl") { continue; }
        const auto stamp = fs::last_write_time(it->path(), error);
        if (error) { continue; }
        if (!found || stamp > out) {
            out = stamp;
            found = true;
        }
    }
    return found;
}

bool is_shader_source(const fs::path &path)
{
    static const std::vector<std::string> kStageExtensions = {
        ".vert", ".frag", ".comp", ".geom", ".rgen", ".rchit", ".rmiss", ".tesc", ".tese"
    };
    const std::string extension = path.extension().string();
    for (const auto &candidate : kStageExtensions) {
        if (extension == candidate) { return true; }
    }
    return false;
}

}// namespace

// Both the build-time compiler (Scripts/Windows/compile-shaders.ps1) and the
// runtime fallback (ShaderHelper::compileShader) used to reuse a .spv whenever
// it merely EXISTED, with no timestamp check. Every shader edit after the first
// build was then silently ignored and the GPU executed stale SPIR-V - a
// fragment shader edited at 14:00 was still being rendered from a .spv produced
// at 18:46 the previous day, which invalidated hours of debugging.
//
// This test fails if that regresses: after a build, no committed .spv may be
// older than the shader it came from.
TEST(BuildIntegrity, CompiledShadersAreNotOlderThanTheirSources)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path shader_root = repo_root / "Resources" / "Shaders";
    ASSERT_TRUE(fs::exists(shader_root)) << "missing " << shader_root.string();

    std::vector<std::string> stale;
    std::error_code error;
    for (fs::recursive_directory_iterator it(shader_root, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        const fs::path &source = it->path();
        if (!it->is_regular_file(error) || !is_shader_source(source)) { continue; }

        const fs::path spv = source.parent_path() / "spv" / (source.filename().string() + ".spv");
        if (!fs::exists(spv, error)) { continue; }// never compiled: not this test's concern

        const auto source_time = fs::last_write_time(source, error);
        if (error) { continue; }
        const auto spv_time = fs::last_write_time(spv, error);
        if (error) { continue; }

        if (spv_time < source_time) {
            stale.push_back(fs::relative(spv, repo_root).string());
        }
    }

    EXPECT_TRUE(stale.empty()) << "SPIR-V older than its source - the GPU would run stale shaders. "
                              << "Stale binaries (" << stale.size() << "): "
                              << [&stale] {
                                     std::string joined;
                                     for (const auto &entry : stale) { joined += "\n  " + entry; }
                                     return joined;
                                 }();
}

// A shader include (*.glsl) that is newer than a .spv means the dependent
// shader was not recompiled. compile-shaders.ps1 accounts for includes; this
// guards that behaviour.
TEST(BuildIntegrity, CompiledShadersAreNotOlderThanSharedIncludes)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty());

    const fs::path shader_root = repo_root / "Resources" / "Shaders";
    ASSERT_TRUE(fs::exists(shader_root));

    fs::file_time_type newest_include{};
    if (!newest_shared_include(shader_root, newest_include)) { GTEST_SKIP() << "no shared shader includes"; }

    std::error_code error;
    std::vector<std::string> stale;
    for (fs::recursive_directory_iterator it(shader_root, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        if (!it->is_regular_file(error) || it->path().extension() != ".spv") { continue; }
        const auto spv_time = fs::last_write_time(it->path(), error);
        if (error) { continue; }
        if (spv_time < newest_include) { stale.push_back(fs::relative(it->path(), repo_root).string()); }
    }

    EXPECT_TRUE(stale.empty()) << stale.size()
                               << " SPIR-V binaries are older than the newest shared include; "
                                  "editing a shared .glsl must rebuild its dependents.";
}

// GLM_FORCE_DEPTH_ZERO_TO_ONE used to be defined ONLY in App.cpp - a
// translation unit that does no projection math - so camera and shadow
// projections were built with OpenGL's [-1,1] depth range while feeding a
// Vulkan [0,1] API. It is now set PUBLIC on VulkanEngineCore, which propagates
// to everything that links it (including this test).
//
// This test deliberately does NOT define the macro itself: it verifies the
// propagation. If someone removes the target_compile_definitions, projections
// silently revert to OpenGL conventions and every depth-dependent feature
// (shadow cascades above all) misbehaves in ways that are painful to trace.
TEST(BuildIntegrity, GlmProducesVulkanDepthRange)
{
    constexpr float kNear = 1.0F;
    constexpr float kFar = 100.0F;
    const glm::mat4 projection = glm::perspective(glm::radians(60.0F), 1.0F, kNear, kFar);

    // View space looks down -Z, so the near/far planes are at -kNear/-kFar.
    const glm::vec4 near_clip = projection * glm::vec4(0.0F, 0.0F, -kNear, 1.0F);
    const glm::vec4 far_clip = projection * glm::vec4(0.0F, 0.0F, -kFar, 1.0F);

    const float near_ndc = near_clip.z / near_clip.w;
    const float far_ndc = far_clip.z / far_clip.w;

    EXPECT_NEAR(near_ndc, 0.0F, 1e-3F)
      << "near plane should map to NDC z=0 (Vulkan). Got " << near_ndc
      << " - GLM_FORCE_DEPTH_ZERO_TO_ONE is not reaching this translation unit.";
    EXPECT_NEAR(far_ndc, 1.0F, 1e-3F)
      << "far plane should map to NDC z=1 (Vulkan). Got " << far_ndc;

    // glm::ortho must agree - the shadow cascades depend on it.
    const glm::mat4 ortho = glm::ortho(-1.0F, 1.0F, -1.0F, 1.0F, kNear, kFar);
    const float ortho_near = (ortho * glm::vec4(0.0F, 0.0F, -kNear, 1.0F)).z;
    EXPECT_NEAR(ortho_near, 0.0F, 1e-3F) << "glm::ortho near plane should map to 0, got " << ortho_near;
}

// EVERY shader source must produce SPIR-V - not just the ones a pipeline
// currently loads.
//
// compile-shaders.ps1 only warns when glslc fails, so a shader that stopped
// compiling left its previous .spv in place and the build stayed green. That
// hid a missing include path (the shader ROOT was never passed to glslc, so
// every `#include "hostDevice/..."` failed) for long enough that the ten
// affected shaders were written off as un-portable "legacy OpenGL-era" files
// in the docs. They compile fine. Worse, rasterizer/shader.frag - a shader the
// main pipeline loads every frame - was among them, so edits to it silently
// did nothing.
TEST(BuildIntegrity, EveryShaderSourceHasCompiledBinary)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty());

    const fs::path shader_root = repo_root / "Resources" / "Shaders";
    std::vector<std::string> missing;

    std::error_code error;
    for (fs::recursive_directory_iterator it(shader_root, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        const fs::path &source = it->path();
        if (!it->is_regular_file(error) || !is_shader_source(source)) { continue; }

        const fs::path spv = source.parent_path() / "spv" / (source.filename().string() + ".spv");
        if (!fs::exists(spv, error)) { missing.push_back(fs::relative(source, repo_root).string()); }
    }

    EXPECT_TRUE(missing.empty()) << missing.size()
                                 << " shader source(s) have no SPIR-V, which means glslc failed and the "
                                    "build only warned: "
                                 << [&missing] {
                                        std::string joined;
                                        for (const auto &entry : missing) { joined += "\n  " + entry; }
                                        return joined;
                                    }();
}

// Every shader the Vulkan pipelines actually load must have a compiled .spv.
// A missing binary only surfaces at pipeline creation, i.e. at runtime on a
// machine that may not be yours.
TEST(BuildIntegrity, ActivePipelineShadersHaveCompiledBinaries)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty());

    const std::vector<std::string> required = {
        "rasterizer/shader.vert", "rasterizer/shader.frag",
        "rasterizer/shadows/directional_shadow_map.vert",
        "rasterizer/shadows/directional_shadow_map.geom",
        "rasterizer/shadows/directional_shadow_map.frag",
        "deferred/geometry.vert", "deferred/geometry.frag",
        "deferred/lighting.vert", "deferred/lighting.frag",
        "post/post.vert", "post/post.frag",
        "skybox/SkyBox.vert", "skybox/SkyBox.frag",
    };

    std::vector<std::string> missing;
    for (const auto &relative : required) {
        const fs::path source = repo_root / "Resources" / "Shaders" / relative;
        if (!fs::exists(source)) { continue; }// shader itself moved - not this test's job
        const fs::path spv = source.parent_path() / "spv" / (source.filename().string() + ".spv");
        if (!fs::exists(spv)) { missing.push_back(relative); }
    }

    EXPECT_TRUE(missing.empty()) << missing.size()
                                 << " active pipeline shaders have no compiled SPIR-V; pipeline "
                                    "creation would fail at runtime.";
}
