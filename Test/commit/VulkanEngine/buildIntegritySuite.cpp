// Regression guards for build-system bugs that were expensive to diagnose.
//
// These are filesystem checks, not GPU tests: they run anywhere, including CI
// containers without an adapter.

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "common/host_device_shared_vars.hpp"

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

// compile-slang-shaders.ps1 names each compiled artifact
// "<source-stem>.<entry-point>.<ext>", where <source-stem> is the .slang
// filename with only the ".slang" extension removed - it may itself contain
// a dot, e.g. "raytrace.rchit" for raytracing/raytrace.rchit.slang - and
// <entry-point> is a manifest entry-point name, which never contains a dot.
// So the source stem is recovered by dropping the LAST dot-separated
// component of the .spv's own stem (the entry point), not the first.
fs::path source_for_spirv(const fs::path &spv_path, const fs::path &spirv_root, const fs::path &slang_root)
{
    const fs::path relative_dir = fs::relative(spv_path.parent_path(), spirv_root);
    const std::string stem_and_entry = spv_path.stem().string();// strips only ".spv"

    const auto last_dot = stem_and_entry.find_last_of('.');
    if (last_dot == std::string::npos) { return {}; }// no entry-point separator: not a manifest artifact

    const std::string source_stem = stem_and_entry.substr(0, last_dot);
    return slang_root / relative_dir / (source_stem + ".slang");
}

// mtime of the most recently edited shared Slang module under common/, or
// false if there are none. Slang has no preprocessor #include; modules under
// common/ are pulled in via `import` and play the role .glsl includes used
// to - editing one must be treated as editing every dependent shader.
bool newest_shared_import(const fs::path &slang_root, fs::file_time_type &out)
{
    const fs::path common_dir = slang_root / "common";
    std::error_code error;
    bool found = false;
    for (fs::recursive_directory_iterator it(common_dir, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        if (!it->is_regular_file(error) || it->path().extension() != ".slang") { continue; }
        const auto stamp = fs::last_write_time(it->path(), error);
        if (error) { continue; }
        if (!found || stamp > out) {
            out = stamp;
            found = true;
        }
    }
    return found;
}

// Subdirectories of Resources/ShadersSlang/ that the C++ Vulkan engine
// actually loads compiled SPIR-V from - see every `slang_spv_dir` constant
// under Src/. Everything else (bloom, ssao, forward, sky, ibl, gpu_cull,
// tonemap, tex_quad, depth_resolve, occlusion_bbox, histogram, ...) is a
// Rust/WebGPU shader that only ever emits WGSL and must not be scanned here.
const std::vector<std::string> kEngineSpirvSubdirs = {
    "compute", "deferred", "path_tracing", "post", "rasterizer", "raytracing", "skybox"
};

// A .slang file is only ever compiled on its own if slangc can find an entry
// point in it. Files that exist purely to be `import`ed (e.g.
// raytracing/rt_types.slang) never appear in compile-slang-shaders.ps1's
// manifest and must not be expected to have a matching .spv.
bool has_entry_point(const fs::path &slang_source)
{
    std::ifstream file(slang_source);
    if (!file) { return false; }
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return content.find("[shader(") != std::string::npos;
}

// True if some .spv directly under spirv_root's mirror of source's directory
// maps back (via source_for_spirv) to exactly this source file.
bool has_compiled_binary_for_source(const fs::path &source, const fs::path &spirv_root, const fs::path &slang_root)
{
    const fs::path relative_source_dir = fs::relative(source.parent_path(), slang_root);
    const fs::path binary_dir = spirv_root / relative_source_dir;

    std::error_code error;
    if (!fs::exists(binary_dir, error)) { return false; }
    for (fs::directory_iterator it(binary_dir, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        if (!it->is_regular_file(error) || it->path().extension() != ".spv") { continue; }
        if (source_for_spirv(it->path(), spirv_root, slang_root) == source) { return true; }
    }
    return false;
}

// Every binding constant shared between host_device_shared_vars.hpp (C++) and
// scene_types.slang (Slang). Both are hand-mirrored today - see
// HostAndShaderSharedConstantsAgree below for why that is dangerous.
const std::vector<std::string> kSharedConstantNames = {
    "MAX_TEXTURE_COUNT", "globalUBO_BINDING", "sceneUBO_BINDING", "OBJECT_DESCRIPTION_BINDING", "TEXTURES_BINDING",
    "SAMPLER_BINDING", "SHADOW_MAP_BINDING", "TLAS_BINDING", "OUT_IMAGE_BINDING", "ACCUMULATION_IMAGE_BINDING"
};

bool is_identifier_char(char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_'; }

// Strips a trailing "// ..." comment so prose mentioning a constant's name
// cannot be mistaken for its definition.
std::string strip_line_comment(const std::string &line)
{
    const auto comment_pos = line.find("//");
    return comment_pos == std::string::npos ? line : line.substr(0, comment_pos);
}

// Parses the integer that follows a constant name at `name_end`, accepting
// both "#define NAME 3" (no '=') and "[static] const int NAME = 3;" (with
// '=' before the digits).
std::optional<int> parse_int_after(const std::string &line, std::size_t name_end)
{
    std::size_t pos = name_end;
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) { ++pos; }
    if (pos < line.size() && line[pos] == '=') {
        ++pos;
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) { ++pos; }
    }

    const std::size_t start = pos;
    if (pos < line.size() && (line[pos] == '-' || line[pos] == '+')) { ++pos; }
    const std::size_t digits_start = pos;
    while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])) != 0) { ++pos; }
    if (pos == digits_start) { return std::nullopt; }// no digits after an optional sign

    return std::stoi(line.substr(start, pos - start));
}

// Scans `path` line by line for every name in kSharedConstantNames, matching
// it as a whole word so e.g. TEXTURES_BINDING does not also match a longer
// identifier that merely contains it as a substring.
std::map<std::string, int> parse_int_constants(const fs::path &path)
{
    std::map<std::string, int> result;
    std::ifstream file(path);
    if (!file) { return result; }

    std::string raw_line;
    while (std::getline(file, raw_line)) {
        const std::string line = strip_line_comment(raw_line);
        for (const auto &name : kSharedConstantNames) {
            if (result.contains(name)) { continue; }

            const std::size_t pos = line.find(name);
            if (pos == std::string::npos) { continue; }

            const bool left_ok = pos == 0 || !is_identifier_char(line[pos - 1]);
            const std::size_t name_end = pos + name.size();
            const bool right_ok = name_end >= line.size() || !is_identifier_char(line[name_end]);
            if (!left_ok || !right_ok) { continue; }

            if (const auto value = parse_int_after(line, name_end)) { result[name] = *value; }
        }
    }
    return result;
}

// Every distinct GTest suite name (a TEST(...)/TEST_F(...) macro's first
// argument) defined anywhere under Test/commit/VulkanEngine. Matches only
// lines whose first non-whitespace characters are the macro name, so a suite
// name appearing in a comment or a string literal is not picked up.
std::set<std::string> collect_defined_suites(const fs::path &tests_dir)
{
    std::set<std::string> suites;
    std::error_code error;
    for (fs::recursive_directory_iterator it(tests_dir, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        if (!it->is_regular_file(error) || it->path().extension() != ".cpp") { continue; }

        std::ifstream file(it->path());
        if (!file) { continue; }
        std::string line;
        while (std::getline(file, line)) {
            const std::size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos) { continue; }

            for (const std::string &macro : { std::string("TEST_F("), std::string("TEST(") }) {
                if (line.compare(start, macro.size(), macro) != 0) { continue; }

                const std::size_t name_start = start + macro.size();
                const std::size_t comma = line.find(',', name_start);
                if (comma == std::string::npos) { break; }

                const std::size_t name_begin = line.find_first_not_of(" \t", name_start);
                const std::size_t name_end = line.find_last_not_of(" \t", comma - 1);
                if (name_begin == std::string::npos || name_begin > name_end) { break; }

                suites.insert(line.substr(name_begin, name_end - name_begin + 1));
                break;
            }
        }
    }
    return suites;
}

// Parses the exact suite-name globs out of Windows.yml's hand-written
// `$cpuOnlySuites` PowerShell array (the "Run CPU-only tests inside the
// container" step). Anchored on the array opener and its `-join ':'` closer
// so an unrelated array elsewhere in the file cannot be picked up. Returns
// std::nullopt only if the file cannot be opened.
std::optional<std::vector<std::string>> parse_ci_filter_suites(const fs::path &workflow_path)
{
    std::ifstream file(workflow_path);
    if (!file) { return std::nullopt; }

    std::vector<std::string> suites;
    bool inside_array = false;
    std::string line;
    while (std::getline(file, line)) {
        if (!inside_array) {
            if (line.find("$cpuOnlySuites = @(") != std::string::npos) { inside_array = true; }
            continue;
        }
        if (line.find("-join ':'") != std::string::npos) { break; }

        const std::size_t open_quote = line.find('\'');
        if (open_quote == std::string::npos) { continue; }
        const std::size_t close_quote = line.find('\'', open_quote + 1);
        if (close_quote == std::string::npos) { continue; }

        std::string entry = line.substr(open_quote + 1, close_quote - open_quote - 1);
        static const std::string kGlobSuffix = ".*";
        if (entry.size() > kGlobSuffix.size()
            && entry.compare(entry.size() - kGlobSuffix.size(), kGlobSuffix.size(), kGlobSuffix) == 0) {
            entry.erase(entry.size() - kGlobSuffix.size());
        }
        suites.push_back(entry);
    }
    return suites;
}

// Parses $DepthTexturePatches from compile-slang-shaders.ps1: a hashtable
// keyed by output .wgsl filename, each value an array of `@{ Pattern = ...;
// Replacement = ... }` entries. Returns filename -> number of Pattern
// entries. Anchored on the "$DepthTexturePatches = @{" opener and the "}"
// that closes it, so an unrelated hashtable elsewhere in the script cannot
// be picked up; within that, each key is anchored on "'<name>' = @(" and its
// matching ")" line.
std::map<std::string, int> parse_powershell_wgsl_patch_counts(const fs::path &script_path)
{
    std::map<std::string, int> result;
    std::ifstream file(script_path);
    if (!file) { return result; }

    bool inside_table = false;
    std::string current_key;
    bool in_block = false;
    int count = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (!inside_table) {
            if (line.find("$DepthTexturePatches = @{") != std::string::npos) { inside_table = true; }
            continue;
        }

        const std::size_t first_non_space = line.find_first_not_of(" \t");
        const std::string trimmed = first_non_space == std::string::npos ? std::string{} : line.substr(first_non_space);

        if (!in_block) {
            if (trimmed == "}") { break; }// end of $DepthTexturePatches

            const std::size_t open_quote = line.find('\'');
            if (open_quote == std::string::npos) { continue; }
            const std::size_t close_quote = line.find('\'', open_quote + 1);
            if (close_quote == std::string::npos) { continue; }
            if (line.find("= @(", close_quote) == std::string::npos) { continue; }

            current_key = line.substr(open_quote + 1, close_quote - open_quote - 1);
            in_block = true;
            count = 0;
            continue;
        }

        if (trimmed == ")") {
            result[current_key] = count;
            in_block = false;
            continue;
        }

        if (line.find("Pattern =") != std::string::npos) { ++count; }
    }
    return result;
}

// Parses the `case "$out_name" in ... esac` patch table from
// compile-slang-shaders.sh. A label may list multiple filenames separated by
// '|' (bash case syntax), sharing the sed lines up to the next ";;" -
// counted once per filename so a shared case arm compares fairly against the
// PowerShell side, where each filename owns its own array. Anchored on the
// "case \"$out_name\" in" opener and "esac", so an unrelated case statement
// elsewhere in the script cannot be picked up.
std::map<std::string, int> parse_bash_wgsl_patch_counts(const fs::path &script_path)
{
    std::map<std::string, int> result;
    std::ifstream file(script_path);
    if (!file) { return result; }

    bool inside_case = false;
    std::vector<std::string> active_keys;
    int count = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (!inside_case) {
            if (line.find("case \"$out_name\" in") != std::string::npos) { inside_case = true; }
            continue;
        }

        const std::size_t start = line.find_first_not_of(" \t");
        const std::string trimmed = start == std::string::npos ? std::string{} : line.substr(start);

        if (trimmed == "esac") { break; }

        if (active_keys.empty()) {
            if (trimmed.size() > 1 && trimmed.back() == ')' && trimmed.find(".wgsl") != std::string::npos) {
                const std::string labels = trimmed.substr(0, trimmed.size() - 1);
                std::size_t pos = 0;
                while (pos <= labels.size()) {
                    const std::size_t bar = labels.find('|', pos);
                    const std::string label =
                      labels.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos);
                    if (!label.empty()) { active_keys.push_back(label); }
                    if (bar == std::string::npos) { break; }
                    pos = bar + 1;
                }
                count = 0;
            }
            continue;
        }

        if (trimmed == ";;") {
            for (const auto &key : active_keys) { result[key] = count; }
            active_keys.clear();
            continue;
        }

        if (line.find("sed -i -E") != std::string::npos) { ++count; }
    }
    return result;
}

}// namespace

// Both the build-time compiler (Scripts/Windows/compile-slang-shaders.ps1) and
// the runtime fallback used to reuse a .spv whenever it merely EXISTED, with
// no timestamp check. Every shader edit after the first build was then
// silently ignored and the GPU executed stale SPIR-V - a fragment shader
// edited at 14:00 was still being rendered from a .spv produced at 18:46 the
// previous day, which invalidated hours of debugging.
//
// This test fails if that regresses: after a build, no committed .spv may be
// older than the .slang source it was compiled from.
TEST(BuildIntegrity, CompiledShadersAreNotOlderThanTheirSources)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path slang_root = repo_root / "Resources" / "ShadersSlang";
    const fs::path spirv_root = slang_root / "build" / "spirv";
    ASSERT_TRUE(fs::exists(spirv_root)) << "missing " << spirv_root.string();

    std::vector<std::string> stale;
    std::vector<std::string> unmapped;
    std::error_code error;
    for (fs::recursive_directory_iterator it(spirv_root, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        const fs::path &spv = it->path();
        if (!it->is_regular_file(error) || spv.extension() != ".spv") { continue; }

        const fs::path source = source_for_spirv(spv, spirv_root, slang_root);
        if (source.empty() || !fs::exists(source, error)) {
            unmapped.push_back(fs::relative(spv, repo_root).string());
            continue;
        }

        const auto source_time = fs::last_write_time(source, error);
        if (error) { continue; }
        const auto spv_time = fs::last_write_time(spv, error);
        if (error) { continue; }

        if (spv_time < source_time) { stale.push_back(fs::relative(spv, repo_root).string()); }
    }

    EXPECT_TRUE(unmapped.empty())
      << unmapped.size()
      << " compiled .spv could not be mapped back to a .slang source under Resources/ShadersSlang "
         "(naming contract in compile-slang-shaders.ps1 broken, or a source was deleted after compiling): "
      << [&unmapped] {
             std::string joined;
             for (const auto &entry : unmapped) { joined += "\n  " + entry; }
             return joined;
         }();

    EXPECT_TRUE(stale.empty()) << "SPIR-V older than its source - the GPU would run stale shaders. "
                              << "Stale binaries (" << stale.size() << "): "
                              << [&stale] {
                                     std::string joined;
                                     for (const auto &entry : stale) { joined += "\n  " + entry; }
                                     return joined;
                                 }();
}

// A shared Slang module under common/ (imported by entry-point shaders) that
// is newer than a .spv means the dependent shader was not recompiled.
// compile-slang-shaders.ps1 is conservative about this (any .slang edit
// invalidates every output); this guards that behaviour.
TEST(BuildIntegrity, CompiledShadersAreNotOlderThanSharedIncludes)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty());

    const fs::path slang_root = repo_root / "Resources" / "ShadersSlang";
    const fs::path spirv_root = slang_root / "build" / "spirv";
    ASSERT_TRUE(fs::exists(spirv_root));

    fs::file_time_type newest_import{};
    if (!newest_shared_import(slang_root, newest_import)) { GTEST_SKIP() << "no shared Slang imports under common/"; }

    std::error_code error;
    std::vector<std::string> stale;
    for (fs::recursive_directory_iterator it(spirv_root, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        if (!it->is_regular_file(error) || it->path().extension() != ".spv") { continue; }
        const auto spv_time = fs::last_write_time(it->path(), error);
        if (error) { continue; }
        if (spv_time < newest_import) { stale.push_back(fs::relative(it->path(), repo_root).string()); }
    }

    EXPECT_TRUE(stale.empty()) << stale.size()
                               << " SPIR-V binaries are older than the newest shared Slang import under "
                                  "common/; editing a shared module must rebuild its dependents.";
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

// EVERY Slang source with an entry point, in a subdirectory the C++ engine
// consumes, must produce SPIR-V - not just the ones a pipeline currently
// loads. compile-slang-shaders.ps1 fails the whole script on a slangc error
// (unlike the old glslc-based compile-shaders.ps1, which only warned), but a
// source that was never added to the manifest at all would otherwise go
// unnoticed until pipeline creation.
TEST(BuildIntegrity, EveryShaderSourceHasCompiledBinary)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty());

    const fs::path slang_root = repo_root / "Resources" / "ShadersSlang";
    const fs::path spirv_root = slang_root / "build" / "spirv";

    std::vector<std::string> missing;
    std::error_code error;
    for (const auto &subdir : kEngineSpirvSubdirs) {
        const fs::path source_dir = slang_root / subdir;
        if (!fs::exists(source_dir, error)) { continue; }

        for (fs::recursive_directory_iterator it(source_dir, error), end; it != end; it.increment(error)) {
            if (error) { break; }
            const fs::path &source = it->path();
            if (!it->is_regular_file(error) || source.extension() != ".slang") { continue; }
            if (!has_entry_point(source)) { continue; }// import-only module, e.g. raytracing/rt_types.slang

            if (!has_compiled_binary_for_source(source, spirv_root, slang_root)) {
                missing.push_back(fs::relative(source, repo_root).string());
            }
        }
    }

    EXPECT_TRUE(missing.empty()) << missing.size()
                                 << " shader source(s) have no SPIR-V, which means slangc was never run for "
                                    "them (missing from compile-slang-shaders.ps1's manifest?) or failed: "
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

    const fs::path slang_root = repo_root / "Resources" / "ShadersSlang";
    const fs::path spirv_root = slang_root / "build" / "spirv";

    // Exactly the paths built from the `slang_spv_dir` constants under Src/
    // (Rasterizer.cpp, DeferredRasterizer.cpp, PostStage.cpp, SkyBox.cpp,
    // CascadedShadowMap.cpp, Clouds.cpp, Raytracing.cpp, PathTracing.cpp).
    const std::vector<std::string> required = {
        "rasterizer/rasterizer.vs_main.spv",
        "rasterizer/rasterizer.fs_main.spv",
        "rasterizer/shadows/shadow_map.shadow_vs_main.spv",
        "rasterizer/shadows/shadow_map.shadow_fs_main.spv",
        "deferred/deferred.geometry_vs_main.spv",
        "deferred/deferred.geometry_fs_main.spv",
        "deferred/deferred.lighting_vs_main.spv",
        "deferred/deferred.lighting_fs_main.spv",
        "post/post.vs_main.spv",
        "post/post.fs_main.spv",
        "skybox/skybox.vs_main.spv",
        "skybox/skybox.fs_main.spv",
        "path_tracing/path_tracing.path_tracing_main.spv",
        "raytracing/raytrace.rgen.rgen_main.spv",
        "raytracing/raytrace.rchit.rchit_main.spv",
        "raytracing/raytrace.rmiss.rmiss_main.spv",
        "raytracing/shadow.rmiss.shadow_rmiss_main.spv",
        "compute/clouds.clouds_main.spv",
        "compute/noise.noise_main.spv",
    };

    std::vector<std::string> missing;
    for (const auto &relative : required) {
        const fs::path spv = spirv_root / relative;
        const fs::path source = source_for_spirv(spv, spirv_root, slang_root);
        if (!source.empty() && !fs::exists(source)) { continue; }// shader itself moved - not this test's job
        if (!fs::exists(spv)) { missing.push_back(relative); }
    }

    EXPECT_TRUE(missing.empty()) << missing.size()
                                 << " active pipeline shaders have no compiled SPIR-V; pipeline "
                                    "creation would fail at runtime.";
}

// host_device_shared_vars.hpp (C++) and scene_types.slang (Slang) hand-mirror
// the same ten binding constants with no shared source of truth. A silent
// divergence would corrupt every descriptor binding without a validation
// error, because each side is internally self-consistent. Asserting every
// name is present in BOTH files (not just equal where both happen to match)
// means a renamed constant fails loudly instead of the pair silently going
// unchecked.
TEST(BuildIntegrity, HostAndShaderSharedConstantsAgree)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const auto host = parse_int_constants(repo_root / "Src" / "GraphicsEngineVulkan" / "common"
                                           / "host_device_shared_vars.hpp");
    const auto shader = parse_int_constants(repo_root / "Resources" / "ShadersSlang" / "common" / "scene_types.slang");

    for (const auto &name : kSharedConstantNames) {
        ASSERT_TRUE(host.contains(name)) << name << " not found (or not parseable) in host_device_shared_vars.hpp";
        ASSERT_TRUE(shader.contains(name)) << name << " not found (or not parseable) in scene_types.slang";
        EXPECT_EQ(host.at(name), shader.at(name))
          << name << " differs between host_device_shared_vars.hpp (" << host.at(name) << ") and scene_types.slang ("
          << shader.at(name) << ')';
    }
}

// The test above parses host_device_shared_vars.hpp as text, so it would
// happily agree with a header edit that the actual build never sees (e.g. a
// stray duplicate definition later in the file, or a macro guarded out by an
// #ifdef). This test instead includes the real header and compares the
// Slang-side values against the constants the compiler actually produced.
TEST(BuildIntegrity, SharedConstantsMatchTheCompiledHostValues)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const auto shader = parse_int_constants(repo_root / "Resources" / "ShadersSlang" / "common" / "scene_types.slang");
    for (const auto &name : kSharedConstantNames) {
        ASSERT_TRUE(shader.contains(name)) << name << " not found (or not parseable) in scene_types.slang";
    }

    EXPECT_EQ(shader.at("MAX_TEXTURE_COUNT"), MAX_TEXTURE_COUNT);
    EXPECT_EQ(shader.at("globalUBO_BINDING"), globalUBO_BINDING);
    EXPECT_EQ(shader.at("sceneUBO_BINDING"), sceneUBO_BINDING);
    EXPECT_EQ(shader.at("OBJECT_DESCRIPTION_BINDING"), OBJECT_DESCRIPTION_BINDING);
    EXPECT_EQ(shader.at("TEXTURES_BINDING"), TEXTURES_BINDING);
    EXPECT_EQ(shader.at("SAMPLER_BINDING"), SAMPLER_BINDING);
    EXPECT_EQ(shader.at("SHADOW_MAP_BINDING"), SHADOW_MAP_BINDING);
    EXPECT_EQ(shader.at("TLAS_BINDING"), TLAS_BINDING);
    EXPECT_EQ(shader.at("OUT_IMAGE_BINDING"), OUT_IMAGE_BINDING);
    EXPECT_EQ(shader.at("ACCUMULATION_IMAGE_BINDING"), ACCUMULATION_IMAGE_BINDING);
}

// A suite added to Test/commit/VulkanEngine that is never added to
// Windows.yml's `$cpuOnlySuites` filter silently does not run in CI - that
// happened once (`eb077041` added `PushConstantRasterizerUnit` to the filter
// by hand only after a planner pass checked all 29 suite names against the
// workflow one at a time). This test makes that check automatic instead of a
// planning cycle: every CPU suite defined under Test/commit/VulkanEngine must
// be either in the filter or in the explicit, justified GPU-exclusion list
// below, and every filter entry must name a suite that still exists (so a
// renamed or deleted suite cannot leave a dead glob silently matching
// nothing).
TEST(BuildIntegrity, EveryCpuSuiteIsInTheWindowsCiFilter)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path workflow_path = repo_root / ".github" / "workflows" / "Windows.yml";
    const auto filter_suites_opt = parse_ci_filter_suites(workflow_path);
    if (!filter_suites_opt.has_value()) {
        GTEST_SKIP() << "could not open " << workflow_path.string() << " - not running from the repo root?";
    }
    const std::vector<std::string> &filter_suites = *filter_suites_opt;
    ASSERT_FALSE(filter_suites.empty())
      << "parsed zero suites out of the $cpuOnlySuites array in " << workflow_path.string()
      << " - the anchor text ('$cpuOnlySuites = @(' / \"-join ':'\") may have changed";
    const std::set<std::string> filter_set(filter_suites.begin(), filter_suites.end());

    const std::set<std::string> defined_suites =
      collect_defined_suites(repo_root / "Test" / "commit" / "VulkanEngine");
    ASSERT_FALSE(defined_suites.empty()) << "found zero TEST()/TEST_F() suites under Test/commit/VulkanEngine - "
                                            "the scan itself is broken";

    // The container ships the Vulkan loader, so SKIP_WITHOUT_GPU's
    // glfwVulkanSupported() check can answer "yes" with no physical device
    // present, after which device creation aborts the process rather than
    // skipping. These stay out of the CI filter until a GPU-capable
    // self-hosted runner exists.
    const std::set<std::string> gpu_excluded_suites = { "GoldenRender", "Integration" };

    std::vector<std::string> missing_from_filter;
    for (const auto &suite : defined_suites) {
        if (filter_set.contains(suite) || gpu_excluded_suites.contains(suite)) { continue; }
        missing_from_filter.push_back(suite);
    }
    EXPECT_TRUE(missing_from_filter.empty())
      << missing_from_filter.size()
      << " suite(s) under Test/commit/VulkanEngine are neither in Windows.yml's $cpuOnlySuites filter nor in "
         "the GPU-exclusion list, so they silently do not run in CI: "
      << [&missing_from_filter] {
             std::string joined;
             for (const auto &entry : missing_from_filter) { joined += "\n  " + entry; }
             return joined;
         }();

    std::vector<std::string> dead_filter_entries;
    for (const auto &suite : filter_suites) {
        if (!defined_suites.contains(suite)) { dead_filter_entries.push_back(suite); }
    }
    EXPECT_TRUE(dead_filter_entries.empty())
      << dead_filter_entries.size()
      << " entry/entries in Windows.yml's $cpuOnlySuites filter do not correspond to any suite under "
         "Test/commit/VulkanEngine (renamed or deleted?): "
      << [&dead_filter_entries] {
             std::string joined;
             for (const auto &entry : dead_filter_entries) { joined += "\n  " + entry; }
             return joined;
         }();
}

// compile-slang-shaders.ps1 and compile-slang-shaders.sh each hand-maintain
// their own copy of the depth-texture WGSL patch table (one PowerShell
// hashtable, one bash `case`), because Slang's WGSL backend has no
// depth-texture resource type and every emitted depth/shadow texture
// declaration needs a post-emit regex fix. Nothing pins the two copies
// together: a patch added to fix one platform and forgotten on the other
// ships broken WGSL on whichever platform builds it last. This test parses
// both as text and asserts they agree on which output files are patched and
// how many regex substitutions apply to each.
TEST(BuildIntegrity, SlangWgslPatchTablesAgree)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path windows_script = repo_root / "Scripts" / "Windows" / "compile-slang-shaders.ps1";
    const fs::path linux_script = repo_root / "Scripts" / "Linux" / "compile-slang-shaders.sh";

    const auto windows_counts = parse_powershell_wgsl_patch_counts(windows_script);
    const auto linux_counts = parse_bash_wgsl_patch_counts(linux_script);

    ASSERT_FALSE(windows_counts.empty())
      << "parsed zero entries out of $DepthTexturePatches in " << windows_script.string()
      << " - the anchor text ('$DepthTexturePatches = @{') may have changed";
    ASSERT_FALSE(linux_counts.empty()) << "parsed zero entries out of the case over $out_name in "
                                       << linux_script.string()
                                       << R"( - the anchor text ('case "$out_name" in') may have changed)";

    std::vector<std::string> windows_only;
    for (const auto &[name, windows_count] : windows_counts) {
        const auto it = linux_counts.find(name);
        if (it == linux_counts.end()) {
            windows_only.push_back(name);
            continue;
        }
        EXPECT_EQ(windows_count, it->second)
          << name << " has " << windows_count << " patch(es) in " << windows_script.string() << " but "
          << it->second << " in " << linux_script.string();
    }
    EXPECT_TRUE(windows_only.empty())
      << windows_only.size() << " file(s) patched in " << windows_script.string() << " but not in "
      << linux_script.string() << ':' << [&windows_only] {
             std::string joined;
             for (const auto &entry : windows_only) { joined += "\n  " + entry; }
             return joined;
         }();

    std::vector<std::string> linux_only;
    for (const auto &entry : linux_counts) {
        if (!windows_counts.contains(entry.first)) { linux_only.push_back(entry.first); }
    }
    EXPECT_TRUE(linux_only.empty())
      << linux_only.size() << " file(s) patched in " << linux_script.string() << " but not in "
      << windows_script.string() << ':' << [&linux_only] {
             std::string joined;
             for (const auto &entry : linux_only) { joined += "\n  " + entry; }
             return joined;
         }();
}

namespace {

// file (relative to Src/GraphicsEngineVulkan/, forward slashes) : line
// (1-based, of the ".value" read) -> a deliberate exception to the rule
// below, with the reason it does not need ASSERT_VULKAN. This is not a way
// to silence a real gap - every entry must be justified.
struct AllowlistEntry
{
    std::string file;
    int line;
};

const std::vector<AllowlistEntry> kCheckedResultAllowlist = {
    // Pipeline cache is a performance optimization, not a correctness
    // requirement: a corrupt/stale on-disk cache (e.g. after a driver
    // update) must not be fatal. Already checked a few lines above - falls
    // back to a null cache and logs a warning instead of aborting.
    { "vulkan_base/VulkanDevice.cpp", 231 },
    // Already fatal: spdlog::critical + std::abort() a few lines above,
    // just not spelled with the ASSERT_VULKAN macro (the message embeds the
    // numeric vk::Result, which the macro's fixed string cannot).
    { "vulkan_base/ShaderHelper.cpp", 37 },
    // Deliberately non-fatal: cloud noise generation is a one-shot compute
    // dispatch. If the transient command pool fails to create, the dispatch
    // is skipped (logged) rather than aborting the whole renderer over an
    // atmospheric effect.
    { "scene/atmospheric_effects/clouds/Clouds.cpp", 249 },
};

bool is_allowlisted_result_check(const std::string &relative_file, int line)
{
    return std::any_of(kCheckedResultAllowlist.begin(), kCheckedResultAllowlist.end(), [&](const AllowlistEntry &entry) {
        return entry.line == line && entry.file == relative_file;
    });
}

// True if `line` calls something that looks like a Vulkan/VMA creation or
// allocation function: the keyword immediately followed by an uppercase
// letter (Vulkan's camelCase naming, e.g. "createDescriptorPool",
// "vmaCreateAllocator") and, after any further identifier characters, an
// opening paren. The uppercase requirement is what tells a real Vulkan call
// apart from an unrelated identifier that merely contains the keyword, such
// as std::filesystem::create_directories(...) or a "..._create_info"
// struct-field reference - both continue with '_', not a capital letter.
// The left-boundary check similarly rejects "recreateSwapChain(", where
// "create" is not a word start. Reuses is_identifier_char from the
// constant-parsing helpers above.
bool looks_like_creation_call(const std::string &line)
{
    static const std::vector<std::string> keywords = { "create", "Create", "allocate", "Allocate" };
    for (const auto &keyword : keywords) {
        std::size_t pos = 0;
        while ((pos = line.find(keyword, pos)) != std::string::npos) {
            const bool left_ok = pos == 0 || !is_identifier_char(line[pos - 1]);
            const std::size_t after_keyword = pos + keyword.size();
            const bool camel_case_continuation =
              after_keyword < line.size() && std::isupper(static_cast<unsigned char>(line[after_keyword])) != 0;
            if (left_ok && camel_case_continuation) {
                std::size_t scan = after_keyword;
                while (scan < line.size() && is_identifier_char(line[scan])) { ++scan; }
                while (scan < line.size() && std::isspace(static_cast<unsigned char>(line[scan])) != 0) { ++scan; }
                if (scan < line.size() && line[scan] == '(') { return true; }
            }
            pos += keyword.size();
        }
    }
    return false;
}

}// namespace

// The instance-extension check used to detect a missing extension, log it,
// and then build a vk::Instance from a createInstance() call whose
// ResultValue was never checked one line later - a failure would silently
// continue with a null-handle instance. Exceptions are disabled project-wide
// (VULKAN_HPP_NO_EXCEPTIONS), so ASSERT_VULKAN's log-critical-and-abort is the
// only fail-fast mechanism available; a missed check is a straight path to a
// null-handle dereference downstream.
//
// This test scans every .cpp under Src/GraphicsEngineVulkan/ for a
// vk::ResultValue::value read with no ASSERT_VULKAN nearby. It cannot
// understand control flow, so it looks in a window around the ".value" read
// on both sides - the check-then-assign idiom used throughout the codebase
// puts ASSERT_VULKAN just above the read, but Rasterizer.cpp and
// DeferredRasterizer.cpp instead assign inside an `if (result == eSuccess)`
// with ASSERT_VULKAN in the `else`, which is a few lines *below* the read.
TEST(BuildIntegrity, VulkanCreationResultsAreChecked)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path engine_root = repo_root / "Src" / "GraphicsEngineVulkan";
    ASSERT_TRUE(fs::exists(engine_root)) << "missing " << engine_root.string();

    constexpr int kWindow = 8;
    std::vector<std::string> violations;
    std::error_code error;
    for (fs::recursive_directory_iterator it(engine_root, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        const fs::path &path = it->path();
        if (!it->is_regular_file(error) || path.extension() != ".cpp") { continue; }

        std::ifstream file(path);
        if (!file) { continue; }
        std::vector<std::string> lines;
        std::string raw_line;
        while (std::getline(file, raw_line)) { lines.push_back(raw_line); }

        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].find(".value") == std::string::npos) { continue; }

            const std::size_t window_begin = (i >= static_cast<std::size_t>(kWindow)) ? i - kWindow : 0;
            const std::size_t window_end = std::min(lines.size() - 1, i + static_cast<std::size_t>(kWindow));

            bool triggered = false;
            bool asserted = false;
            for (std::size_t w = window_begin; w <= window_end; ++w) {
                if (w <= i && looks_like_creation_call(lines[w])) { triggered = true; }
                if (lines[w].find("ASSERT_VULKAN") != std::string::npos) { asserted = true; }
            }
            if (!triggered || asserted) { continue; }

            const std::string relative_file = fs::relative(path, engine_root).generic_string();
            if (is_allowlisted_result_check(relative_file, static_cast<int>(i + 1))) { continue; }

            violations.push_back(relative_file + ":" + std::to_string(i + 1) + ": " + lines[i]);
        }
    }

    EXPECT_TRUE(violations.empty())
      << violations.size()
      << " Vulkan creation/allocation result(s) read via .value with no ASSERT_VULKAN nearby "
         "(exceptions are disabled project-wide, so a failed creation call must abort rather than "
         "continue into a null-handle dereference; add ASSERT_VULKAN, or for a deliberate exception "
         "add a justified entry to kCheckedResultAllowlist above):"
      << [&violations] {
             std::string joined;
             for (const auto &entry : violations) { joined += "\n  " + entry; }
             return joined;
         }();
}
