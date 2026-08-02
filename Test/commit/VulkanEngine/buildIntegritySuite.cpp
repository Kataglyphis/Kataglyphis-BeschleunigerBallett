// Regression guards for build-system bugs that were expensive to diagnose.
//
// These are filesystem checks, not GPU tests: they run anywhere, including CI
// containers without an adapter.

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ObjectDescription.hpp"
#include "common/host_device_shared_vars.hpp"
#include "renderer/GlobalUBO.hpp"
#include "renderer/PathTracingDispatch.hpp"
#include "renderer/SceneUBO.hpp"
#include "renderer/pushConstants/PushConstantPathTracing.hpp"
#include "renderer/pushConstants/PushConstantPost.hpp"
#include "renderer/pushConstants/PushConstantRasterizer.hpp"
#include "renderer/pushConstants/PushConstantRayTracing.hpp"
#include "scene/atmospheric_effects/clouds/CloudDispatch.hpp"
#include "shared/scene/ObjMaterial.hpp"
#include "shared/scene/Vertex.hpp"

import kataglyphis.vulkan.cascaded_shadow_map;// Kataglyphis::ShadowPushConstants

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

// Resources/ShadersSlang/shader-manifest.json is the SINGLE source of truth
// for the Slang shader build: both Scripts/Windows/compile-slang-shaders.ps1
// and Scripts/Linux/compile-slang-shaders.sh consume it. It replaced the two
// per-script hand-maintained copies of the manifest/WGSL-map/patch tables
// that this suite used to cross-check against each other - with one data
// file there is no second copy left to drift, so the tests below instead
// verify the FILESYSTEM (compiled SPIR-V, checked-in Rust-crate WGSL)
// against that one manifest, and ShaderManifestJsonIsPresentAndWellFormed
// fails loudly if the file goes missing or corrupt, because neither build
// script can run without it.

// One "wgslMap" row: a Slang source whose combined WGSL emit is copied into
// a Rust crate's shader directory. histogram.wgsl is absent by construction:
// it is hand-written, with no generating Slang source (its manifest row is
// marked disabled).
struct WgslMapping
{
    std::string slang_source;// "src": relative to Resources/ShadersSlang/
    std::string dst_dir;     // "dst": relative to the repository root
    std::string wgsl_file;   // "out": destination file name
};

// Everything this suite needs out of shader-manifest.json, parsed once and
// shared (see shader_manifest below).
struct ShaderManifestData
{
    // Relative (to Resources/ShadersSlang/) .slang paths of every enabled
    // manifest row whose targets include 'spirv' - exactly the sources the
    // C++ Vulkan renderer consumes.
    std::set<std::string> vulkan_spirv_sources;
    // Distinct first path components of vulkan_spirv_sources - the
    // build/spirv/ subdirectories the manifest emits into. Every other
    // subdirectory of Resources/ShadersSlang/ (bloom, ssao, forward, sky,
    // ibl, gpu_cull, tonemap, tex_quad, ...) is a Rust/WebGPU shader that
    // only ever emits WGSL and must not be scanned for SPIR-V.
    std::set<std::string> engine_spirv_subdirs;
    std::vector<WgslMapping> wgsl_map;
    // Output filenames keyed by "depthTexturePatches" (documentation
    // "_comment" keys excluded).
    std::set<std::string> depth_patched_files;
    // "minSlangcVersionForWgsl": the toolchain floor both compile scripts use
    // to decide whether the combined WGSL emit may run at all. Empty when the
    // key is absent - ShaderManifestPinsAMinimumSlangcVersionForWgsl fails on
    // that, because an absent floor silently re-enables the broken emit.
    std::string min_slangc_version_for_wgsl;
};

// Strict parse of shader-manifest.json: any missing/mistyped field in a row
// returns std::nullopt rather than skipping the row, so a malformed manifest
// fails ShaderManifestJsonIsPresentAndWellFormed loudly instead of silently
// shrinking every JSON-derived check. Exceptions are disabled project-wide,
// so nlohmann's no-throw parse mode is used and every access is type-checked
// up front instead of relying on at()'s throws.
std::optional<ShaderManifestData> parse_shader_manifest(const fs::path &manifest_path)
{
    std::ifstream file(manifest_path);
    if (!file) { return std::nullopt; }

    const nlohmann::json doc = nlohmann::json::parse(file, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) { return std::nullopt; }

    const auto manifest_it = doc.find("manifest");
    if (manifest_it == doc.end() || !manifest_it->is_array()) { return std::nullopt; }

    ShaderManifestData data;
    for (const auto &row : *manifest_it) {
        if (!row.is_object()) { return std::nullopt; }

        const auto disabled_it = row.find("disabled");
        if (disabled_it != row.end() && disabled_it->is_boolean() && disabled_it->get<bool>()) { continue; }

        const auto file_it = row.find("file");
        const auto targets_it = row.find("targets");
        if (file_it == row.end() || !file_it->is_string()) { return std::nullopt; }
        if (targets_it == row.end() || !targets_it->is_array() || targets_it->empty()) { return std::nullopt; }

        bool emits_spirv = false;
        for (const auto &target : *targets_it) {
            if (!target.is_string()) { return std::nullopt; }
            if (target.get<std::string>() == "spirv") { emits_spirv = true; }
        }
        if (!emits_spirv) { continue; }

        const std::string source = file_it->get<std::string>();
        data.vulkan_spirv_sources.insert(source);
        const std::size_t slash = source.find('/');
        if (slash != std::string::npos && slash > 0) { data.engine_spirv_subdirs.insert(source.substr(0, slash)); }
    }

    const auto wgsl_map_it = doc.find("wgslMap");
    if (wgsl_map_it == doc.end() || !wgsl_map_it->is_array()) { return std::nullopt; }
    for (const auto &row : *wgsl_map_it) {
        if (!row.is_object()) { return std::nullopt; }
        const auto src_it = row.find("src");
        const auto out_it = row.find("out");
        const auto dst_it = row.find("dst");
        if (src_it == row.end() || !src_it->is_string()) { return std::nullopt; }
        if (out_it == row.end() || !out_it->is_string()) { return std::nullopt; }
        if (dst_it == row.end() || !dst_it->is_string()) { return std::nullopt; }
        data.wgsl_map.push_back({ src_it->get<std::string>(), dst_it->get<std::string>(), out_it->get<std::string>() });
    }

    const auto patches_it = doc.find("depthTexturePatches");
    if (patches_it == doc.end() || !patches_it->is_object()) { return std::nullopt; }
    for (const auto &[key, value] : patches_it->items()) {
        if (key.starts_with("_")) { continue; }// documentation-only keys
        if (!value.is_array() || value.empty()) { return std::nullopt; }
        data.depth_patched_files.insert(key);
    }

    const auto min_version_it = doc.find("minSlangcVersionForWgsl");
    if (min_version_it != doc.end() && min_version_it->is_string()) {
        data.min_slangc_version_for_wgsl = min_version_it->get<std::string>();
    }

    return data;
}

// Parses shader-manifest.json exactly once per process and shares the result
// across every test in this suite. std::nullopt means the file is missing or
// malformed - callers must ASSERT on has_value(), never skip.
const std::optional<ShaderManifestData> &shader_manifest(const fs::path &repo_root)
{
    static const std::optional<ShaderManifestData> cached =
      parse_shader_manifest(repo_root / "Resources" / "ShadersSlang" / "shader-manifest.json");
    return cached;
}

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
    "MAX_TEXTURE_COUNT", "MAX_CASCADES", "globalUBO_BINDING", "sceneUBO_BINDING", "OBJECT_DESCRIPTION_BINDING",
    "TEXTURES_BINDING", "SAMPLER_BINDING", "SHADOW_MAP_BINDING", "TLAS_BINDING", "OUT_IMAGE_BINDING",
    "ACCUMULATION_IMAGE_BINDING"
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

// Every TEST(<suite>, ...) test name defined anywhere under `tests_dir` whose
// suite is exactly `suite`. Same start-of-line anchoring as
// collect_defined_suites, so a name in a comment or a string literal is not
// picked up. Pure file I/O - never runs the tests themselves, which matters
// for GoldenRender/Integration: they require a GPU that the CI container
// does not have.
std::vector<std::string> collect_suite_test_names(const fs::path &tests_dir, const std::string &suite)
{
    std::vector<std::string> names;
    const std::string macro_prefix = "TEST(" + suite + ",";
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
            if (line.compare(start, macro_prefix.size(), macro_prefix) != 0) { continue; }

            const std::size_t name_start = start + macro_prefix.size();
            const std::size_t close_paren = line.find(')', name_start);
            if (close_paren == std::string::npos) { continue; }

            const std::size_t name_begin = line.find_first_not_of(" \t", name_start);
            const std::size_t name_end = line.find_last_not_of(" \t", close_paren - 1);
            if (name_begin == std::string::npos || name_begin > name_end) { continue; }

            names.push_back(line.substr(name_begin, name_end - name_begin + 1));
        }
    }
    return names;
}

// The four named integers in docs/gpu-golden-testing.md's
// `<!-- golden-counts: defined=N runnable=N integration=N total=N -->`
// marker line.
struct GoldenCountsMarker
{
    int defined = 0;
    int runnable = 0;
    int integration = 0;
    int total = 0;
};

std::optional<int> parse_marker_field(const std::string &line, const std::string &key)
{
    const std::size_t pos = line.find(key);
    if (pos == std::string::npos) { return std::nullopt; }

    const std::size_t digits_start = pos + key.size();
    std::size_t digits_end = digits_start;
    while (digits_end < line.size() && std::isdigit(static_cast<unsigned char>(line[digits_end]))) { ++digits_end; }
    if (digits_end == digits_start) { return std::nullopt; }

    return std::stoi(line.substr(digits_start, digits_end - digits_start));
}

// Parses docs/gpu-golden-testing.md's golden-counts marker line. Returns
// std::nullopt if the marker line, or any of its four fields, is missing -
// the caller distinguishes that from "file not found" so a deleted marker is
// a hard failure rather than a silent pass.
std::optional<GoldenCountsMarker> parse_golden_counts_marker(const fs::path &doc_path)
{
    std::ifstream file(doc_path);
    if (!file) { return std::nullopt; }

    std::string line;
    while (std::getline(file, line)) {
        if (line.find("<!-- golden-counts:") == std::string::npos) { continue; }

        const auto defined_val = parse_marker_field(line, "defined=");
        const auto runnable_val = parse_marker_field(line, "runnable=");
        const auto integration_val = parse_marker_field(line, "integration=");
        const auto total_val = parse_marker_field(line, "total=");
        if (!defined_val || !runnable_val || !integration_val || !total_val) { return std::nullopt; }

        GoldenCountsMarker marker;
        marker.defined = *defined_val;
        marker.runnable = *runnable_val;
        marker.integration = *integration_val;
        marker.total = *total_val;
        return marker;
    }
    return std::nullopt;
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

// Every fuzz-target name declared via kataglyphis_add_fuzz_test(<name> ...)
// in Test/fuzz/CMakeLists.txt. The function definition itself
// ("function(kataglyphis_add_fuzz_test fuzz_target source_file)") does not
// match: there is a space, not '(', right after the macro name there.
std::vector<std::string> parse_declared_fuzz_targets(const fs::path &cmake_path)
{
    std::vector<std::string> targets;
    std::ifstream file(cmake_path);
    if (!file) { return targets; }

    static const std::string kMacro = "kataglyphis_add_fuzz_test(";
    std::string line;
    while (std::getline(file, line)) {
        const std::size_t pos = line.find(kMacro);
        if (pos == std::string::npos) { continue; }

        const std::size_t name_start = pos + kMacro.size();
        std::size_t name_end = name_start;
        while (name_end < line.size() && is_identifier_char(line[name_end])) { ++name_end; }
        if (name_end == name_start) { continue; }
        targets.push_back(line.substr(name_start, name_end - name_start));
    }
    return targets;
}

// Parses the fuzz-target names out of Windows.yml's "Run fuzz target seeds
// inside the container" step: a PowerShell `foreach (`$t in @('a','b',...))`
// loop. Anchored on "foreach (`$t in @(" (the backtick escapes $t inside the
// surrounding double-quoted PowerShell string) and the following "))", so an
// unrelated foreach loop elsewhere in the file cannot be picked up. Returns
// std::nullopt only if the file cannot be opened; an empty vector means the
// anchor text itself was not found, which the caller must fail loudly on
// rather than skip.
std::optional<std::vector<std::string>> parse_ci_fuzz_targets(const fs::path &workflow_path)
{
    std::ifstream file(workflow_path);
    if (!file) { return std::nullopt; }

    // Two spellings, both legal: the step used to run in a host-side pwsh
    // block where `$t` needed backtick-escaping so the RUNNER did not expand
    // it; since the step moved to ContainerHub's run-in-windows-container
    // action the command is passed through env, so it is a plain `$t`. Accept
    // either rather than pinning the test to one CI plumbing style.
    static const std::array<std::string, 2> kAnchors = { "foreach (`$t in @(", "foreach ($t in @(" };
    static const std::string kCloser = "))";

    std::string line;
    while (std::getline(file, line)) {
        std::size_t anchor_pos = std::string::npos;
        std::size_t anchor_size = 0;
        for (const auto &anchor : kAnchors) {
            const std::size_t pos = line.find(anchor);
            if (pos != std::string::npos) {
                anchor_pos = pos;
                anchor_size = anchor.size();
                break;
            }
        }
        if (anchor_pos == std::string::npos) { continue; }

        const std::size_t list_start = anchor_pos + anchor_size;
        const std::size_t closer_pos = line.find(kCloser, list_start);
        if (closer_pos == std::string::npos) { break; }

        const std::string list = line.substr(list_start, closer_pos - list_start);
        std::vector<std::string> targets;
        std::size_t pos = 0;
        while (pos < list.size()) {
            const std::size_t open_quote = list.find('\'', pos);
            if (open_quote == std::string::npos) { break; }
            const std::size_t close_quote = list.find('\'', open_quote + 1);
            if (close_quote == std::string::npos) { break; }
            targets.push_back(list.substr(open_quote + 1, close_quote - open_quote - 1));
            pos = close_quote + 1;
        }
        return targets;
    }
    return std::vector<std::string>{};
}

using Kataglyphis::ShadowPushConstants;
using Kataglyphis::VulkanRendererInternals::DirectionalLightData;
using Kataglyphis::VulkanRendererInternals::GlobalUBO;
using Kataglyphis::VulkanRendererInternals::PushConstantPathTracing;
using Kataglyphis::VulkanRendererInternals::PushConstantPost;
using Kataglyphis::VulkanRendererInternals::PushConstantRasterizer;
using Kataglyphis::VulkanRendererInternals::PushConstantRaytracing;
using Kataglyphis::VulkanRendererInternals::SceneUBO;

// --- SPIR-V struct-offset parsing, backing SharedStructOffsetsMatchTheCompiledSpirv ---
//
// Mirrors Kataglyphis::validateSpirvBlob's magic-number check
// (vulkan_base/ShaderHelper.cpp) - that constant lives in an anonymous
// namespace inside a module implementation unit and cannot be included from
// here, so the literal is re-declared rather than reused.
constexpr uint32_t kSpirvMagicNumber = 0x07230203;
constexpr std::size_t kSpirvHeaderWordCount = 5;
constexpr uint32_t kOpName = 5;
constexpr uint32_t kOpMemberName = 6;
constexpr uint32_t kOpMemberDecorate = 72;
constexpr uint32_t kDecorationOffset = 35;

// Decodes a SPIR-V literal string operand: ASCII/UTF-8 bytes packed 4 per
// word (little end first), NUL-terminated and padded to a word boundary.
std::string spirv_literal_string(const std::vector<uint32_t> &words, std::size_t word_start, std::size_t word_count)
{
    std::string text;
    text.reserve(word_count * 4);
    for (std::size_t i = 0; i < word_count; ++i) {
        const uint32_t word = words[word_start + i];
        for (uint32_t shift = 0; shift < 32U; shift += 8U) {
            const auto ch = static_cast<char>((word >> shift) & 0xFFU);
            if (ch == '\0') { return text; }
            text.push_back(ch);
        }
    }
    return text;
}

// Parses a compiled .spv module for every named struct's per-member byte
// Offset decoration: { struct name as emitted (e.g. "SceneUBO_std140") ->
// { member name -> Offset } }. Returns std::nullopt if the file cannot be
// opened or does not start with the SPIR-V magic number - callers treat
// that as "not a SPIR-V module" rather than byte-swapping, the same
// contract as Kataglyphis::validateSpirvBlob.
std::optional<std::map<std::string, std::map<std::string, uint32_t>>> parse_spirv_member_offsets(
  const fs::path &spv_path)
{
    std::ifstream file(spv_path, std::ios::binary);
    if (!file) { return std::nullopt; }
    const std::vector<char> raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (raw.size() < kSpirvHeaderWordCount * sizeof(uint32_t) || raw.size() % sizeof(uint32_t) != 0) {
        return std::nullopt;
    }

    std::vector<uint32_t> words(raw.size() / sizeof(uint32_t));
    std::memcpy(words.data(), raw.data(), raw.size());
    if (words[0] != kSpirvMagicNumber) { return std::nullopt; }

    std::map<uint32_t, std::string> type_names;// OpName: id -> name
    std::map<std::pair<uint32_t, uint32_t>, std::string> member_names;// OpMemberName: (type id, member) -> name
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> member_offsets;// OpMemberDecorate Offset: (type id, member) -> offset

    std::size_t pos = kSpirvHeaderWordCount;
    while (pos < words.size()) {
        const uint32_t instruction_word = words[pos];
        const uint32_t word_count = instruction_word >> 16U;
        const uint32_t opcode = instruction_word & 0xFFFFU;
        if (word_count == 0 || pos + word_count > words.size()) { break; }// malformed stream - stop, do not read OOB

        if (opcode == kOpName && word_count >= 2) {
            type_names[words[pos + 1]] = spirv_literal_string(words, pos + 2, word_count - 2);
        } else if (opcode == kOpMemberName && word_count >= 3) {
            member_names[{ words[pos + 1], words[pos + 2] }] = spirv_literal_string(words, pos + 3, word_count - 3);
        } else if (opcode == kOpMemberDecorate && word_count >= 4) {
            if (words[pos + 3] == kDecorationOffset && word_count >= 5) {
                member_offsets[{ words[pos + 1], words[pos + 2] }] = words[pos + 4];
            }
        }

        pos += word_count;
    }

    std::map<std::string, std::map<std::string, uint32_t>> result;
    for (const auto &[ids, offset] : member_offsets) {
        const auto type_it = type_names.find(ids.first);
        const auto member_it = member_names.find(ids);
        if (type_it == type_names.end() || member_it == member_names.end()) { continue; }
        result[type_it->second][member_it->second] = offset;
    }
    return result;
}

// One shared-layout struct's contract: the struct name Slang emits it as in
// compiled SPIR-V, paired with { emitted member name -> offsetof(HostType,
// member) }. ArrayStride/MatrixStride are deliberately not checked here -
// those live on OpDecorate of the pointer/array types, not OpMemberDecorate
// of the struct, and need type-id chasing this pass does not do; that is a
// scope decision, not an oversight.
struct SpirvStructContract
{
    std::string spirv_name;
    std::map<std::string, std::size_t> member_offsets;
};

// PushConstantSkyBox_std430 deliberately has no entry below: SkyBox.cpp:334,
// 411 pushes a bare sizeof(uint32_t) with no host struct to compare against.
std::vector<SpirvStructContract> build_shared_struct_offset_contracts()
{
    return {
        { "SceneUBO_std140",
          { { "dirLight", offsetof(SceneUBO, dirLight) },
            { "pcfRadius", offsetof(SceneUBO, pcfRadius) },
            { "cascadedShadowIntensity", offsetof(SceneUBO, cascadedShadowIntensity) },
            { "numCascades", offsetof(SceneUBO, numCascades) },
            { "cascadeSplits", offsetof(SceneUBO, cascadeSplits) },
            { "cascadeLightSpaceMatrices", offsetof(SceneUBO, cascadeLightSpaceMatrices) },
            { "view_dir", offsetof(SceneUBO, view_dir) },
            { "cam_pos", offsetof(SceneUBO, cam_pos) },
            { "cloudMovementDirection", offsetof(SceneUBO, cloudMovementDirection) },
            { "cloudMeshScale", offsetof(SceneUBO, cloudMeshScale) },
            { "cloudMeshOffset", offsetof(SceneUBO, cloudMeshOffset) },
            { "cloudParameters", offsetof(SceneUBO, cloudParameters) } } },
        { "GlobalUBO_std140",
          { { "projection", offsetof(GlobalUBO, projection) },
            { "view", offsetof(GlobalUBO, view) },
            { "inv_projection", offsetof(GlobalUBO, inv_projection) },
            { "inv_view", offsetof(GlobalUBO, inv_view) } } },
        // skybox.slang re-declares GlobalUBO's four members verbatim under its
        // own ConstantBuffer name - same host type (GlobalUBO), different
        // emitted struct name.
        { "CameraUBO_std140",
          { { "projection", offsetof(GlobalUBO, projection) },
            { "view", offsetof(GlobalUBO, view) },
            { "inv_projection", offsetof(GlobalUBO, inv_projection) },
            { "inv_view", offsetof(GlobalUBO, inv_view) } } },
        { "DirectionalLightData_std140",
          { { "direction", offsetof(DirectionalLightData, direction) },
            { "color", offsetof(DirectionalLightData, color) } } },
        { "ObjectDescription_std430",
          { { "vertex_address", offsetof(ObjectDescription, vertex_address) },
            { "index_address", offsetof(ObjectDescription, index_address) },
            { "material_index_address", offsetof(ObjectDescription, material_index_address) },
            { "material_address", offsetof(ObjectDescription, material_address) },
            { "texture_offset", offsetof(ObjectDescription, texture_offset) } } },
        { "ObjMaterial_natural",
          { { "ambient", offsetof(ObjMaterial, ambient) },
            { "diffuse", offsetof(ObjMaterial, diffuse) },
            { "specular", offsetof(ObjMaterial, specular) },
            { "transmittance", offsetof(ObjMaterial, transmittance) },
            { "emission", offsetof(ObjMaterial, emission) },
            { "shininess", offsetof(ObjMaterial, shininess) },
            { "ior", offsetof(ObjMaterial, ior) },
            { "dissolve", offsetof(ObjMaterial, dissolve) },
            { "illum", offsetof(ObjMaterial, illum) },
            { "textureID", offsetof(ObjMaterial, textureID) },
            { "alphaCutoff", offsetof(ObjMaterial, alphaCutoff) },
            { "uv_scale", offsetof(ObjMaterial, uv_scale) },
            { "uv_offset", offsetof(ObjMaterial, uv_offset) } } },
        { "Vertex_natural",
          { { "position", offsetof(Vertex, position) },
            { "normal", offsetof(Vertex, normal) },
            { "color", offsetof(Vertex, color) },
            { "texture_coords", offsetof(Vertex, texture_coords) } } },
        { "PushConstantRasterizer_std430",
          { { "model", offsetof(PushConstantRasterizer, model) },
            { "invModelRows", offsetof(PushConstantRasterizer, invModelRows) },
            { "objectIndex", offsetof(PushConstantRasterizer, objectIndex) } } },
        { "PushConstantPathTracing_std430",
          { { "clearColor", offsetof(PushConstantPathTracing, clearColor) },
            { "width", offsetof(PushConstantPathTracing, width) },
            { "height", offsetof(PushConstantPathTracing, height) },
            { "frame_index", offsetof(PushConstantPathTracing, frame_index) },
            { "samples_per_pixel", offsetof(PushConstantPathTracing, samples_per_pixel) },
            { "max_bounces", offsetof(PushConstantPathTracing, max_bounces) } } },
        { "PushConstantPost_std430",
          { { "aspect_ratio", offsetof(PushConstantPost, aspect_ratio) },
            { "clouds_enabled", offsetof(PushConstantPost, clouds_enabled) },
            { "shadows_enabled", offsetof(PushConstantPost, shadows_enabled) },
            { "skybox_enabled", offsetof(PushConstantPost, skybox_enabled) } } },
        { "PushConstantRaytracing_std430",
          { { "clear_color", offsetof(PushConstantRaytracing, clear_color) } } },
        { "ShadowPushConstants_std430",
          // shadow_map.slang names its second field objectIndex, while the
          // host Kataglyphis::ShadowPushConstants (CascadedShadowMap.ixx)
          // calls the same field cascadeIndex - CascadedShadowMap.cpp:504
          // actually passes the flat object index through it
          // (makeShadowPush(modelMatrix, object_index)), so the field is
          // genuinely the shader's objectIndex under an older host name.
          // Keying on the emitted name here checks the two sides agree on
          // BYTE OFFSET, which is what the GPU actually reads.
          { { "model", offsetof(ShadowPushConstants, model) },
            { "objectIndex", offsetof(ShadowPushConstants, cascadeIndex) } } },
    };
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

    const auto &manifest = shader_manifest(repo_root);
    ASSERT_TRUE(manifest.has_value()) << "shader-manifest.json is missing or malformed";

    std::vector<std::string> missing;
    std::error_code error;
    for (const auto &subdir : manifest->engine_spirv_subdirs) {
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
    EXPECT_EQ(shader.at("MAX_CASCADES"), MAX_CASCADES);
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

// The fuzz step's target list in Windows.yml is a hand-maintained array, and
// the step silently `continue`s past a missing executable rather than
// failing - the same class of gap `EveryCpuSuiteIsInTheWindowsCiFilter`
// closes for gtest suites, one step away in the same file: a fuzz target
// declared in Test/fuzz/CMakeLists.txt but never added to Windows.yml's
// array does not run in CI, and nothing says so.
TEST(BuildIntegrity, EveryFuzzTargetIsInTheWindowsCiFuzzList)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const std::vector<std::string> declared_targets =
      parse_declared_fuzz_targets(repo_root / "Test" / "fuzz" / "CMakeLists.txt");
    ASSERT_FALSE(declared_targets.empty())
      << "parsed zero kataglyphis_add_fuzz_test(...) declarations out of Test/fuzz/CMakeLists.txt - the "
         "anchor text ('kataglyphis_add_fuzz_test(') may have changed";
    const std::set<std::string> declared_set(declared_targets.begin(), declared_targets.end());

    const fs::path workflow_path = repo_root / ".github" / "workflows" / "Windows.yml";
    const auto ci_targets_opt = parse_ci_fuzz_targets(workflow_path);
    if (!ci_targets_opt.has_value()) {
        GTEST_SKIP() << "could not open " << workflow_path.string() << " - not running from the repo root?";
    }
    const std::vector<std::string> &ci_targets = *ci_targets_opt;
    ASSERT_FALSE(ci_targets.empty())
      << "parsed zero fuzz targets out of the foreach array in " << workflow_path.string()
      << R"( - the anchor text ('foreach (`$t in @(' / '))') may have changed)";
    const std::set<std::string> ci_set(ci_targets.begin(), ci_targets.end());

    // FuzzTest smoke targets (dummy.cpp / example_fuzz_test.cpp) - they exist to
    // prove the fuzzing harness itself works, not to cover engine surface, so
    // Windows.yml deliberately does not run them.
    const std::set<std::string> excluded_from_ci = { "first_fuzz_test", "example_fuzz_test" };

    std::vector<std::string> missing_from_ci;
    for (const auto &target : declared_targets) {
        if (ci_set.contains(target) || excluded_from_ci.contains(target)) { continue; }
        missing_from_ci.push_back(target);
    }
    EXPECT_TRUE(missing_from_ci.empty())
      << missing_from_ci.size()
      << " fuzz target(s) declared in Test/fuzz/CMakeLists.txt are neither in Windows.yml's fuzz-seed foreach "
         "array nor in the smoke-target exclusion list, so they silently do not run in CI: "
      << [&missing_from_ci] {
             std::string joined;
             for (const auto &entry : missing_from_ci) { joined += "\n  " + entry; }
             return joined;
         }();

    std::vector<std::string> dead_ci_entries;
    for (const auto &target : ci_targets) {
        if (!declared_set.contains(target)) { dead_ci_entries.push_back(target); }
    }
    EXPECT_TRUE(dead_ci_entries.empty())
      << dead_ci_entries.size()
      << " entry/entries in Windows.yml's fuzz-seed foreach array do not correspond to any target declared "
         "in Test/fuzz/CMakeLists.txt (renamed or deleted?): "
      << [&dead_ci_entries] {
             std::string joined;
             for (const auto &entry : dead_ci_entries) { joined += "\n  " + entry; }
             return joined;
         }();
}

// SlangWgslPatchTablesAgree and SlangCompileManifestsAgree lived here until
// 2026-08-02. Both existed only to pin two hand-maintained copies of the same
// data against each other - the manifest and the depth-texture patch table,
// duplicated across compile-slang-shaders.ps1 and .sh. Both copies are gone:
// Resources/ShadersSlang/shader-manifest.json is now the single source both
// scripts read, so there is no second table left to disagree with. The
// remaining tests below verify the FILESYSTEM against that manifest, which is
// the check that still has teeth.

// CompiledShadersAreNotOlderThanTheirSources guards the SPIR-V artifacts; the
// checked-in Rust-crate WGSL artifacts (the manifest's wgslMap) have no
// equivalent guard, and they live two directories away
// (ExternalLib/Kataglyphis-RustProjectTemplate/crates/.../shaders) from the
// Slang source that generates them. A regenerate that drops one of the
// depth-texture patches, or a .slang edit that never gets propagated, is
// silent today. This walks the wgslMap and asserts each checked-in .wgsl is
// not older than its source.
TEST(BuildIntegrity, CheckedInWgslIsNotOlderThanItsSlangSource)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path slang_root = repo_root / "Resources" / "ShadersSlang";

    const auto &manifest = shader_manifest(repo_root);
    ASSERT_TRUE(manifest.has_value()) << "shader-manifest.json is missing or malformed";

    std::vector<std::string> stale;
    int checked = 0;
    for (const auto &mapping : manifest->wgsl_map) {
        const fs::path source = slang_root / mapping.slang_source;
        ASSERT_TRUE(fs::exists(source))
          << "Slang source mapped by the manifest's wgslMap is missing: " << source.string();

        const fs::path dest = repo_root / mapping.dst_dir / mapping.wgsl_file;
        if (!fs::exists(dest)) { continue; }// RustProjectTemplate submodule not checked out here

        std::error_code error;
        const auto source_time = fs::last_write_time(source, error);
        if (error) { continue; }
        const auto dest_time = fs::last_write_time(dest, error);
        if (error) { continue; }

        ++checked;
        if (dest_time < source_time) {
            stale.push_back(fs::relative(dest, repo_root).string() + " (mtime ticks=" + std::to_string(dest_time.time_since_epoch().count())
                             + ") is older than " + fs::relative(source, repo_root).string()
                             + " (mtime ticks=" + std::to_string(source_time.time_since_epoch().count()) + ')');
        }
    }

    if (checked == 0) {
        GTEST_SKIP() << "none of the checked-in Rust-crate WGSL destinations exist - the "
                        "RustProjectTemplate submodule is likely not checked out here";
    }

    EXPECT_TRUE(stale.empty()) << stale.size()
                               << " checked-in Rust-crate WGSL file(s) are older than the Slang source that "
                                  "generates them (regenerate via compile-slang-shaders.ps1/.sh): "
                               << [&stale] {
                                      std::string joined;
                                      for (const auto &entry : stale) { joined += "\n  " + entry; }
                                      return joined;
                                  }();
}

// WGSL has no string literals, so a "//" can only ever appear as the start of
// a comment - the Slang WGSL backend itself emits none. A "//" in a checked-in
// destination from the manifest's wgslMap is therefore always a hand-edit made
// directly on the generated file, with a regenerate's expiry date on it: the
// next compile-slang-shaders run silently drops it. Catch it here instead.
TEST(BuildIntegrity, CheckedInWgslHasNoHandEdits)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const auto &manifest = shader_manifest(repo_root);
    ASSERT_TRUE(manifest.has_value()) << "shader-manifest.json is missing or malformed";

    std::vector<std::string> hand_edits;
    int checked = 0;
    for (const auto &mapping : manifest->wgsl_map) {
        const fs::path dest = repo_root / mapping.dst_dir / mapping.wgsl_file;
        if (!fs::exists(dest)) { continue; }// RustProjectTemplate submodule not checked out here
        ++checked;

        std::ifstream file(dest);
        if (!file) { continue; }

        std::string line;
        int line_number = 0;
        while (std::getline(file, line)) {
            ++line_number;
            if (line.find("//") != std::string::npos) {
                hand_edits.push_back(fs::relative(dest, repo_root).string() + ':' + std::to_string(line_number)
                                      + ": " + line);
            }
        }
    }

    if (checked == 0) {
        GTEST_SKIP() << "none of the checked-in Rust-crate WGSL destinations exist - the "
                        "RustProjectTemplate submodule is likely not checked out here";
    }

    EXPECT_TRUE(hand_edits.empty())
      << hand_edits.size()
      << " line(s) with '//' found in checked-in generated WGSL - generated WGSL must not be hand-edited - put "
         "the change in the .slang source, or in the post-emit patch table in "
         "compile-slang-shaders.ps1/.sh: "
      << [&hand_edits] {
             std::string joined;
             for (const auto &entry : hand_edits) { joined += "\n  " + entry; }
             return joined;
         }();
}

// WGSL requires every non-builtin member of an inter-stage (varying) struct to
// carry @location(N); only @builtin members may omit it. slangc
// 2026.1-52-gc8ddf20bb (Vulkan SDK 1.4.341.1 - the ContainerHub Linux image)
// drops @location from varying structs in the COMBINED emit (compiled without
// -entry/-stage, which is exactly how the manifest's wgslMap files are
// produced) while emitting it correctly per entry point from the SAME binary;
// slangc 2026.8 is correct on both Windows and Linux and reproduces these
// files byte-for-byte. A regeneration on the older toolchain therefore turned
// `@location(0) uv_0 : vec2<f32>` into a bare `uv_0 : vec2<f32>` in eight of
// the ten checked-in files - WGSL naga rejects, committed silently because
// nothing looked at the emit. The compile scripts now skip the emit below the
// manifest's minSlangcVersionForWgsl and hard-fail on a violation above it;
// this is the backstop that runs in CI on every platform and cannot be
// bypassed by regenerating with a different tool.
//
// A struct with at least one @builtin/@location member is an IO struct, so
// every member of it must carry one of those attributes. Structs with no such
// member (uniform/storage layouts, which use @align instead) are not IO and
// are skipped.
TEST(BuildIntegrity, CheckedInWgslVaryingStructsCarryLocations)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const auto &manifest = shader_manifest(repo_root);
    ASSERT_TRUE(manifest.has_value()) << "shader-manifest.json is missing or malformed";

    static const std::regex kStructHead(R"(^struct\s+([A-Za-z_]\w*))");
    static const std::regex kMember(R"(^\s*((?:@\w+\([^)]*\)\s*)*)([A-Za-z_]\w*)\s*:\s*\S.*?,?\s*$)");
    const auto is_io_attr = [](const std::string &attrs) {
        return attrs.find("@builtin(") != std::string::npos || attrs.find("@location(") != std::string::npos;
    };

    std::vector<std::string> violations;
    int checked = 0;
    for (const auto &mapping : manifest->wgsl_map) {
        const fs::path dest = repo_root / mapping.dst_dir / mapping.wgsl_file;
        if (!fs::exists(dest)) { continue; }// RustProjectTemplate submodule not checked out here
        ++checked;

        std::ifstream file(dest);
        if (!file) { continue; }

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') { line.pop_back(); }
            lines.push_back(line);
        }

        std::size_t index = 0;
        while (index < lines.size()) {
            std::smatch head;
            const std::string struct_line = lines[index];
            ++index;
            if (!std::regex_search(struct_line, head, kStructHead)) { continue; }
            const std::string struct_name = head[1].str();

            if (index < lines.size() && lines[index].find_first_not_of(" \t") != std::string::npos
                && lines[index].substr(lines[index].find_first_not_of(" \t")) == "{") {
                ++index;
            }

            // (1-based line number, attribute prefix, raw text) per member.
            std::vector<std::tuple<std::size_t, std::string, std::string>> members;
            while (index < lines.size()) {
                const std::size_t first = lines[index].find_first_not_of(" \t");
                if (first != std::string::npos && lines[index][first] == '}') { break; }
                std::smatch member;
                if (std::regex_match(lines[index], member, kMember)) {
                    members.emplace_back(index + 1, member[1].str(), lines[index]);
                }
                ++index;
            }

            const bool is_io_struct = std::any_of(members.begin(), members.end(), [&](const auto &member) {
                return is_io_attr(std::get<1>(member));
            });
            if (!is_io_struct) { continue; }

            for (const auto &[line_number, attrs, text] : members) {
                if (is_io_attr(attrs)) { continue; }
                violations.push_back(fs::relative(dest, repo_root).generic_string() + ':'
                                      + std::to_string(line_number) + ": struct " + struct_name + ": " + text);
            }
        }
    }

    if (checked == 0) {
        GTEST_SKIP() << "none of the checked-in Rust-crate WGSL destinations exist - the "
                        "RustProjectTemplate submodule is likely not checked out here";
    }

    EXPECT_TRUE(violations.empty())
      << violations.size()
      << " member(s) of an inter-stage WGSL struct carry neither @builtin nor @location - naga rejects that. "
         "This is the signature of a regeneration with slangc older than the manifest's "
         "minSlangcVersionForWgsl; regenerate with a newer slangc rather than hand-editing: "
      << [&violations] {
             std::string joined;
             for (const auto &entry : violations) { joined += "\n  " + entry; }
             return joined;
         }();
}

// The floor above is only enforced if it is actually in the manifest: both
// compile scripts treat a missing minSlangcVersionForWgsl as "no floor" (they
// must, so an older manifest still builds), which would silently re-enable the
// broken combined emit on the container's slangc. Pin its presence and shape
// here instead - the scripts compare the leading MAJOR.MINOR only.
TEST(BuildIntegrity, ShaderManifestPinsAMinimumSlangcVersionForWgsl)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const auto &manifest = shader_manifest(repo_root);
    ASSERT_TRUE(manifest.has_value()) << "shader-manifest.json is missing or malformed";

    EXPECT_FALSE(manifest->min_slangc_version_for_wgsl.empty())
      << "shader-manifest.json has no \"minSlangcVersionForWgsl\" - without it both compile scripts stop "
         "skipping the combined WGSL emit on toolchains whose emit drops varying @location attributes";
    EXPECT_TRUE(std::regex_search(manifest->min_slangc_version_for_wgsl, std::regex(R"(^\d+\.\d+)")))
      << "\"minSlangcVersionForWgsl\" (" << manifest->min_slangc_version_for_wgsl
      << ") must start with MAJOR.MINOR - the compile scripts compare only that prefix and treat anything "
         "unparseable as new enough";
}

// `Resources/Shaders/` (the pre-Slang GLSL tree) was deleted once the Slang
// migration finished; every .slang file is now the sole source for its
// shader. A handful of header comments still said "Mirrors
// Resources/Shaders/..." for months afterward, pointing a reader at a tree
// that no longer exists instead of at the file they were already reading -
// the same failure mode as trusting stale SPIR-V above: a comment claiming
// the authoritative version lives elsewhere. This walks every .slang under
// Resources/ShadersSlang/ (excluding the build/ output directory) and fails
// naming any file plus line that still references the deleted path.
TEST(BuildIntegrity, SlangSourcesDoNotReferenceTheDeletedGlslTree)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path slang_root = repo_root / "Resources" / "ShadersSlang";
    ASSERT_TRUE(fs::exists(slang_root)) << "missing " << slang_root.string();

    static const std::string kDeadPath = "Resources/Shaders";

    std::vector<std::string> violations;
    std::error_code error;
    for (fs::recursive_directory_iterator it(slang_root, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        const fs::path &path = it->path();
        if (!it->is_regular_file(error) || path.extension() != ".slang") { continue; }
        if (fs::relative(path, slang_root).generic_string().starts_with("build/")) { continue; }

        std::ifstream file(path);
        if (!file) { continue; }
        std::string line;
        int line_number = 0;
        while (std::getline(file, line)) {
            ++line_number;
            if (line.find(kDeadPath) != std::string::npos) {
                violations.push_back(fs::relative(path, repo_root).generic_string() + ':'
                                      + std::to_string(line_number) + ": " + line);
            }
        }
    }

    EXPECT_TRUE(violations.empty())
      << violations.size() << " line(s) under " << slang_root.string() << " still reference the deleted "
      << kDeadPath << " tree - update the comment to describe the .slang file as the sole source: "
      << [&violations] {
             std::string joined;
             for (const auto &entry : violations) { joined += "\n  " + entry; }
             return joined;
         }();
}

// scene_types.slang's MAX_CASCADES (:68) is the gated source of truth for the
// cascade count - HostAndShaderSharedConstantsAgree above pins it against
// host_device_shared_vars.hpp. That gate is blind to a second, independent
// "static const int" redeclaring the same value under a cascade-ish name
// elsewhere: raising MAX_CASCADES on both currently-gated sides would pass
// every existing pin test while a shader still reading its own stale local
// copy kept clamping to the old count. shadow_map.slang carried exactly this
// (`NUM_CASCADES = 3`) until it was retired in favour of importing
// MAX_CASCADES directly. This scans every other .slang file for a
// "static const int" whose name contains "CASCADE" (case-insensitive) and
// whose value equals MAX_CASCADES, and fails if one exists.
//
// The scan is restricted to Vulkan-consumed shaders (per
// parse_vulkan_consumed_slang_sources, i.e. the compile-slang-shaders.sh
// manifest rows that target 'spirv') rather than every .slang file: WGSL-only
// shaders such as forward/forward.slang pin their own cascade count on the
// Rust side (see forward.slang's "Must match CASCADE_COUNT in forward.rs"
// comment) and cannot go stale against this C++-side MAX_CASCADES - the C++
// engine never loads them, so a redeclaration there is not the bug this gate
// exists to catch.
TEST(BuildIntegrity, NoShaderRedeclaresTheCascadeCount)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path slang_root = repo_root / "Resources" / "ShadersSlang";
    ASSERT_TRUE(fs::exists(slang_root)) << "missing " << slang_root.string();

    const fs::path scene_types_relative = fs::path("common") / "scene_types.slang";
    const auto scene_types_constants = parse_int_constants(slang_root / scene_types_relative);
    ASSERT_TRUE(scene_types_constants.contains("MAX_CASCADES"))
      << "MAX_CASCADES not found (or not parseable) in " << (slang_root / scene_types_relative).string();
    const int max_cascades = scene_types_constants.at("MAX_CASCADES");

    const auto &manifest = shader_manifest(repo_root);
    ASSERT_TRUE(manifest.has_value()) << "shader-manifest.json is missing or malformed";
    const auto &vulkan_consumed_sources = manifest->vulkan_spirv_sources;
    ASSERT_FALSE(vulkan_consumed_sources.empty())
      << "no spirv-targeted rows in shader-manifest.json - manifest format changed?";

    static const std::string kDeclKeyword = "static const int ";

    std::vector<std::string> violations;
    std::error_code error;
    for (fs::recursive_directory_iterator it(slang_root, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        const fs::path &path = it->path();
        if (!it->is_regular_file(error) || path.extension() != ".slang") { continue; }
        const std::string relative_path = fs::relative(path, slang_root).generic_string();
        if (relative_path.starts_with("build/")) { continue; }
        if (relative_path == scene_types_relative.generic_string()) { continue; }
        if (!vulkan_consumed_sources.contains(relative_path)) { continue; }

        std::ifstream file(path);
        if (!file) { continue; }
        std::string raw_line;
        int line_number = 0;
        while (std::getline(file, raw_line)) {
            ++line_number;
            const std::string line = strip_line_comment(raw_line);
            const auto keyword_pos = line.find(kDeclKeyword);
            if (keyword_pos == std::string::npos) { continue; }

            std::size_t name_start = keyword_pos + kDeclKeyword.size();
            std::size_t name_end = name_start;
            while (name_end < line.size() && is_identifier_char(line[name_end])) { ++name_end; }
            if (name_end == name_start) { continue; }

            const std::string name = line.substr(name_start, name_end - name_start);
            const auto value = parse_int_after(line, name_end);
            if (!value) { continue; }

            std::string lower_name = name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (*value == max_cascades && lower_name.find("cascade") != std::string::npos) {
                violations.push_back(fs::relative(path, repo_root).generic_string() + ':'
                                      + std::to_string(line_number) + ": " + name + " = " + std::to_string(*value));
            }
        }
    }

    EXPECT_TRUE(violations.empty())
      << violations.size()
      << " shader-local cascade-count constant(s) redeclare MAX_CASCADES (" << max_cascades
      << ") outside scene_types.slang - import MAX_CASCADES instead: "
      << [&violations] {
             std::string joined;
             for (const auto &entry : violations) { joined += "\n  " + entry; }
             return joined;
         }();
}

namespace {

// A text-only call-graph reachability check for the whole Slang corpus (see
// EverySlangFunctionIsReachableFromAnEntryPoint below). No real Slang front
// end is available to this suite, so this is a tokenizer, not a parser: it
// extracts identifiers and a handful of punctuation marks and reasons about
// brace/paren nesting depth, nothing more.

// One identifier or one punctuation mark of interest ("(){}:;,") found while
// scanning a file. Whitespace, numeric literals, and every other character
// are not tokenized at all - see tokenize_slang below for why numeric
// literals need special handling.
struct SlangToken
{
    std::string text;
    std::size_t offset = 0;// into the comment/string-stripped text this token came from
    bool is_identifier = false;
};

// Tokenizes comment/string-stripped Slang source text into SlangTokens.
// Numeric literals (including a letter suffix, e.g. "8u", "1.0f") are
// consumed and discarded as a single opaque run rather than left for the
// generic scan - otherwise a suffix letter like the 'u' in "8u" would start
// its own spurious one-character identifier token.
std::vector<SlangToken> tokenize_slang(const std::string &text)
{
    std::vector<SlangToken> tokens;
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (std::isspace(ch) != 0) {
            ++i;
            continue;
        }
        if (std::isdigit(ch) != 0) {
            while (i < text.size()
                   && (std::isalnum(static_cast<unsigned char>(text[i])) != 0 || text[i] == '.')) {
                ++i;
            }
            continue;
        }
        if (std::isalpha(ch) != 0 || ch == '_') {
            const std::size_t start = i;
            while (i < text.size() && is_identifier_char(text[i])) { ++i; }
            tokens.push_back({ text.substr(start, i - start), start, true });
            continue;
        }
        static const std::string kInteresting = "(){}:;,";
        if (kInteresting.find(static_cast<char>(ch)) != std::string::npos) {
            tokens.push_back({ std::string(1, static_cast<char>(ch)), i, false });
        }
        ++i;
    }
    return tokens;
}

// Blanks out double-quoted string contents (keeping the quotes and every
// other character in place, so offsets are unaffected) - only
// `[shader("...")]`-style attributes carry string literals in this corpus,
// but this is generic rather than special-cased to that attribute.
std::string strip_string_literals(const std::string &line)
{
    std::string result = line;
    bool in_string = false;
    for (char &ch : result) {
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) { ch = ' '; }
    }
    return result;
}

// True once `line`, trimmed, is empty.
bool is_blank_line(const std::string &line) { return line.find_first_not_of(" \t\r") == std::string::npos; }

// True if `line`, trimmed, starts with '[' - every Slang attribute
// ([shader(...)], [numthreads(...)], [vk::binding(...)], ...) in this corpus
// is written on its own line directly above the declaration it decorates.
bool is_attribute_line(const std::string &line)
{
    const auto first = line.find_first_not_of(" \t\r");
    return first != std::string::npos && line[first] == '[';
}

// Whether the contiguous run of blank/attribute lines immediately preceding
// stripped_lines[def_line_index] contains a `[shader("...")]` attribute -
// the marker that makes a function a call-graph root. Slang stacks multiple
// attributes directly on top of each other with no blank line between them
// (gpu_cull.slang's `cs_main` has `[shader("compute")]` then
// `[numthreads(64, 1, 1)]` immediately above it), so this walks the whole
// contiguous run rather than looking at only the single immediately
// preceding line, which would miss exactly that case.
bool preceded_by_shader_attribute(const std::vector<std::string> &stripped_lines, std::size_t def_line_index)
{
    std::size_t i = def_line_index;
    while (i > 0) {
        --i;
        const std::string &line = stripped_lines[i];
        if (is_blank_line(line)) { continue; }
        if (!is_attribute_line(line)) { break; }
        if (line.find("[shader(") != std::string::npos) { return true; }
    }
    return false;
}

// One function definition found somewhere under Resources/ShadersSlang.
struct SlangFunctionDef
{
    std::string name;
    std::string relative_file;// relative to Resources/ShadersSlang, forward slashes
    int line = 0;             // 1-based, the line the return type starts on
    bool is_root = false;     // preceded by a [shader("...")] attribute
    std::string body;         // comment/string-stripped body text, for call-graph edges
    std::string raw_def_line; // original (unstripped) text of `line`, for allowlist marker lookup
};

// Scans one already comment/string-stripped, tokenized file for function
// definitions: two consecutive identifier tokens (a return type and a name)
// immediately followed by '(' at brace depth 0. This is deliberately not a
// real parser - see EverySlangFunctionIsReachableFromAnEntryPoint's comment
// for why a text gate is enough here - but two lightweight checks keep it
// honest: the parameter list's matching ')' is located by paren-depth
// counting (so multi-line parameter lists such as rasterizer.slang's
// `vs_main` do not break it), and after skipping an optional
// " : SEMANTIC" clause the next token must be '{' - a bare declaration
// (next token ';') is not treated as a definition.
void collect_functions_from_file(const std::vector<std::string> &stripped_lines, const std::vector<std::string> &raw_lines,
                                  const std::string &relative_file, std::vector<SlangFunctionDef> &out)
{
    std::string text;
    std::vector<std::size_t> line_start_offsets;
    line_start_offsets.reserve(stripped_lines.size());
    for (const auto &line : stripped_lines) {
        line_start_offsets.push_back(text.size());
        text += line;
        text += '\n';
    }

    auto line_for_offset = [&](std::size_t offset) -> int {
        const auto it = std::upper_bound(line_start_offsets.begin(), line_start_offsets.end(), offset);
        return static_cast<int>(std::distance(line_start_offsets.begin(), it));// upper_bound - 1, then +1 for 1-based
    };

    const std::vector<SlangToken> tokens = tokenize_slang(text);

    int depth = 0;
    std::size_t t = 0;
    while (t < tokens.size()) {
        const SlangToken &tok = tokens[t];
        if (!tok.is_identifier) {
            if (tok.text == "{") { ++depth; }
            else if (tok.text == "}") { --depth; }
            ++t;
            continue;
        }

        if (depth == 0 && t + 2 < tokens.size() && tokens[t + 1].is_identifier && !tokens[t + 2].is_identifier
            && tokens[t + 2].text == "(") {
            // Find the parameter list's matching ')'.
            int paren_depth = 1;
            std::size_t j = t + 3;
            while (j < tokens.size() && paren_depth > 0) {
                if (!tokens[j].is_identifier && tokens[j].text == "(") { ++paren_depth; }
                else if (!tokens[j].is_identifier && tokens[j].text == ")") { --paren_depth; }
                ++j;
            }
            if (paren_depth == 0) {
                // Skip an optional " : SEMANTIC" clause.
                std::size_t k = j;
                if (k < tokens.size() && !tokens[k].is_identifier && tokens[k].text == ":") {
                    ++k;
                    if (k < tokens.size() && tokens[k].is_identifier) { ++k; }
                }
                if (k < tokens.size() && !tokens[k].is_identifier && tokens[k].text == "{") {
                    // Confirmed definition - locate the matching '}' for the body.
                    int body_depth = 1;
                    std::size_t m = k + 1;
                    while (m < tokens.size() && body_depth > 0) {
                        if (!tokens[m].is_identifier && tokens[m].text == "{") { ++body_depth; }
                        else if (!tokens[m].is_identifier && tokens[m].text == "}") { --body_depth; }
                        ++m;
                    }
                    const std::size_t body_begin_offset = tokens[k].offset;
                    const std::size_t body_end_offset =
                      (m > 0 && m <= tokens.size()) ? (tokens[m - 1].offset + 1) : text.size();

                    SlangFunctionDef fn;
                    fn.name = tokens[t + 1].text;
                    fn.relative_file = relative_file;
                    fn.line = line_for_offset(tok.offset);
                    fn.is_root = preceded_by_shader_attribute(stripped_lines, static_cast<std::size_t>(fn.line - 1));
                    fn.body = text.substr(body_begin_offset, body_end_offset - body_begin_offset);
                    fn.raw_def_line = (fn.line >= 1 && static_cast<std::size_t>(fn.line - 1) < raw_lines.size())
                                         ? raw_lines[static_cast<std::size_t>(fn.line - 1)]
                                         : std::string();
                    out.push_back(std::move(fn));

                    depth = 0;// back to top level once the function's own body has closed
                    t = m;
                    continue;
                }
            }
        }
        ++t;
    }
}

// Every function definition under Resources/ShadersSlang (excluding
// build/), analysed as one global corpus rather than per file - see
// EverySlangFunctionIsReachableFromAnEntryPoint for why.
std::vector<SlangFunctionDef> collect_slang_functions(const fs::path &slang_root)
{
    std::vector<SlangFunctionDef> functions;
    std::error_code error;
    for (fs::recursive_directory_iterator it(slang_root, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        const fs::path &path = it->path();
        if (!it->is_regular_file(error) || path.extension() != ".slang") { continue; }
        const std::string relative_path = fs::relative(path, slang_root).generic_string();
        if (relative_path.starts_with("build/")) { continue; }

        std::ifstream file(path);
        if (!file) { continue; }
        std::vector<std::string> raw_lines;
        std::vector<std::string> stripped_lines;
        std::string raw_line;
        while (std::getline(file, raw_line)) {
            stripped_lines.push_back(strip_line_comment(strip_string_literals(raw_line)));
            raw_lines.push_back(std::move(raw_line));
        }

        collect_functions_from_file(stripped_lines, raw_lines, relative_path, functions);
    }
    return functions;
}

// A deliberate exception to EverySlangFunctionIsReachableFromAnEntryPoint,
// with the reason the function is not reachable from any [shader(...)]
// entry point. Every entry must be matched by a
// "// UNREACHABLE_SLANG_FUNCTION_OK: <marker>" trailing comment on the
// function's definition line (checked below), so an exemption cannot rot
// silently after the line it protects moves or is deleted.
struct UnreachableSlangAllowlistEntry
{
    std::string file;// relative to Resources/ShadersSlang/, forward slashes
    std::string marker;
};

const std::vector<UnreachableSlangAllowlistEntry> kUnreachableSlangAllowlist = {};

const std::string kUnreachableSlangMarkerPrefix = "UNREACHABLE_SLANG_FUNCTION_OK: ";

}// namespace

// Two shipped regressions in one week (the `ibl` batch XIII found, and the
// `forward` one immediately above in this file's history) shared the same
// signature: the .slang source still defined the helper, the emitted output
// no longer contained it, and nothing called it any more - a lost call site,
// not dead code that should have been deleted. This is the general form of
// that check: every function defined anywhere under Resources/ShadersSlang
// must be reachable, by name, from some `[shader("...")]` entry point.
//
// The corpus is analysed globally rather than per file because Slang has no
// preprocessor #include - modules are pulled in via `import` (e.g.
// ibl.slang's `import fullscreen;`), so a helper's caller routinely lives in
// a different file than its definition. Reachability is therefore also by
// NAME rather than by a fully resolved symbol: two unrelated functions that
// happen to share a name (ibl.slang's own `distribution_ggx` and
// common/brdf.slang's) are both treated as reachable if either name is
// called from a root, which can only produce false negatives (a genuinely
// dead function hiding behind a live same-named one), never false
// positives - an acceptable trade for a text gate with no real Slang front
// end behind it.
//
// A dead helper usually means a lost call site, not a helper that should be
// deleted: deleting `sky_radiance` (see forward.slang) would have "fixed"
// this gate and cemented that regression rather than catching it. Triage
// every finding - wire the call back up, or if it is a genuine one-off
// (spike code, a test fixture), justify it in kUnreachableSlangAllowlist
// above rather than deleting the function.
TEST(BuildIntegrity, EverySlangFunctionIsReachableFromAnEntryPoint)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path slang_root = repo_root / "Resources" / "ShadersSlang";
    ASSERT_TRUE(fs::exists(slang_root)) << "missing " << slang_root.string();

    const std::vector<SlangFunctionDef> functions = collect_slang_functions(slang_root);

    // Self-verifying floors: EveryShaderSourceHasCompiledBinary has already
    // been burned once by a parser change that silently found nothing and
    // still read as a pass. These floors are well below the real counts (at
    // time of writing: ~90 functions, ~53 entry points) so ordinary shader
    // edits never bump into them, but a scan that finds (near-)zero of
    // either cannot pass silently.
    const int total_functions = static_cast<int>(functions.size());
    const int total_roots =
      static_cast<int>(std::count_if(functions.begin(), functions.end(), [](const auto &fn) { return fn.is_root; }));
    ASSERT_GT(total_functions, 40) << "found only " << total_functions
                                    << " Slang function definition(s) under " << slang_root.string()
                                    << " - the definition scan itself is broken";
    ASSERT_GT(total_roots, 20) << "found only " << total_roots
                                << " [shader(\"...\")] entry point(s) under " << slang_root.string()
                                << " - the root scan itself is broken";

    std::map<std::string, std::vector<std::size_t>> functions_by_name;
    for (std::size_t idx = 0; idx < functions.size(); ++idx) { functions_by_name[functions[idx].name].push_back(idx); }

    std::vector<bool> reachable(functions.size(), false);
    std::vector<std::size_t> worklist;
    for (std::size_t idx = 0; idx < functions.size(); ++idx) {
        if (functions[idx].is_root) {
            reachable[idx] = true;
            worklist.push_back(idx);
        }
    }

    while (!worklist.empty()) {
        const std::size_t idx = worklist.back();
        worklist.pop_back();

        std::set<std::string> called_names;
        for (const auto &tok : tokenize_slang(functions[idx].body)) {
            if (tok.is_identifier) { called_names.insert(tok.text); }
        }

        for (const auto &name : called_names) {
            const auto found = functions_by_name.find(name);
            if (found == functions_by_name.end()) { continue; }
            for (const std::size_t callee : found->second) {
                if (!reachable[callee]) {
                    reachable[callee] = true;
                    worklist.push_back(callee);
                }
            }
        }
    }

    std::vector<bool> allowlist_entry_matched(kUnreachableSlangAllowlist.size(), false);
    std::vector<std::string> violations;
    for (std::size_t idx = 0; idx < functions.size(); ++idx) {
        if (reachable[idx]) { continue; }
        const SlangFunctionDef &fn = functions[idx];

        int allowlist_index = -1;
        for (std::size_t a = 0; a < kUnreachableSlangAllowlist.size(); ++a) {
            const auto &entry = kUnreachableSlangAllowlist[a];
            if (entry.file != fn.relative_file) { continue; }
            if (fn.raw_def_line.find(kUnreachableSlangMarkerPrefix + entry.marker) == std::string::npos) { continue; }
            allowlist_index = static_cast<int>(a);
            break;
        }
        if (allowlist_index >= 0) {
            allowlist_entry_matched[static_cast<std::size_t>(allowlist_index)] = true;
            continue;
        }

        violations.push_back(fn.relative_file + ":" + std::to_string(fn.line) + ": " + fn.name);
    }

    EXPECT_TRUE(violations.empty())
      << violations.size()
      << " Slang function(s) are not reachable, by name, from any [shader(\"...\")] entry point in "
      << slang_root.string()
      << " - this almost always means a lost call site (see this test's comment), not dead code to delete; wire "
         "the call back up, or if it is deliberate add a \"// UNREACHABLE_SLANG_FUNCTION_OK: <marker>\" comment "
         "on the definition line and a justified entry to kUnreachableSlangAllowlist above:"
      << [&violations] {
             std::string joined;
             for (const auto &entry : violations) { joined += "\n  " + entry; }
             return joined;
         }();

    std::vector<std::string> dead_exemptions;
    for (std::size_t idx = 0; idx < kUnreachableSlangAllowlist.size(); ++idx) {
        if (!allowlist_entry_matched[idx]) {
            dead_exemptions.push_back(kUnreachableSlangAllowlist[idx].file + " (" + kUnreachableSlangAllowlist[idx].marker + ")");
        }
    }
    EXPECT_TRUE(dead_exemptions.empty())
      << dead_exemptions.size()
      << " kUnreachableSlangAllowlist entr(y/ies) matched no unreachable function - the exemption is dead and "
         "must be deleted:"
      << [&dead_exemptions] {
             std::string joined;
             for (const auto &entry : dead_exemptions) { joined += "\n  " + entry; }
             return joined;
         }();
}

namespace {

// One `export module <name>;` declaration found in an .ixx file.
struct ModuleInterface
{
    std::string name;
    fs::path path;// absolute path of the declaring .ixx
};

// Extracts the module name from a line of the form "<prefix><name>;",
// trimming surrounding whitespace. Returns an empty string if `line` does
// not start with `prefix` or has no terminating ';'.
std::string extract_module_name(const std::string &line, const std::string &prefix)
{
    if (line.compare(0, prefix.size(), prefix) != 0) { return {}; }
    const std::string rest = line.substr(prefix.size());
    const auto semicolon = rest.find(';');
    if (semicolon == std::string::npos) { return {}; }

    const std::string name = rest.substr(0, semicolon);
    const auto first = name.find_first_not_of(" \t");
    if (first == std::string::npos) { return {}; }
    const auto last = name.find_last_not_of(" \t");
    return name.substr(first, last - first + 1);
}

// Every `export module <name>;` interface under `src_root`. Primary module
// interfaces only (the project has no module partitions today); a file is
// assumed to declare at most one module.
std::vector<ModuleInterface> collect_module_interfaces(const fs::path &src_root)
{
    std::vector<ModuleInterface> modules;
    std::error_code error;
    for (fs::recursive_directory_iterator it(src_root, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        if (!it->is_regular_file(error) || it->path().extension() != ".ixx") { continue; }

        std::ifstream file(it->path());
        if (!file) { continue; }
        std::string line;
        while (std::getline(file, line)) {
            const std::string name = extract_module_name(line, "export module ");
            if (!name.empty()) {
                modules.push_back({ name, it->path() });
                break;
            }
        }
    }
    return modules;
}

// name -> generic-string paths of every .cpp/.ixx file under `roots`
// containing an `import <name>;` or `export import <name>;` line.
std::map<std::string, std::set<std::string>> collect_module_importers(const std::vector<fs::path> &roots)
{
    std::map<std::string, std::set<std::string>> importers;
    std::error_code error;
    for (const auto &root : roots) {
        for (fs::recursive_directory_iterator it(root, error), end; it != end; it.increment(error)) {
            if (error) { break; }
            const fs::path &path = it->path();
            if (!it->is_regular_file(error)) { continue; }
            const auto extension = path.extension();
            if (extension != ".cpp" && extension != ".ixx") { continue; }

            std::ifstream file(path);
            if (!file) { continue; }
            std::string line;
            while (std::getline(file, line)) {
                std::string name = extract_module_name(line, "export import ");
                if (name.empty()) { name = extract_module_name(line, "import "); }
                if (!name.empty()) { importers[name].insert(path.generic_string()); }
            }
        }
    }
    return importers;
}

}// namespace

// A module interface (.ixx) that nothing imports is still compiled into
// every build - Src/GraphicsEngineVulkan/CMakeLists.txt globs *.ixx into
// VulkanEngineCore's CXX_MODULES file set unconditionally - and is rescanned
// by clang-scan-deps on every configure. Two such modules
// (kataglyphis.shared.scene.vertex / kataglyphis.shared.scene.obj_material)
// existed for no reason but that nobody deleted their wrapper .ixx after the
// equivalent kataglyphis.vulkan.vertex / kataglyphis.vulkan.obj_material
// modules were introduced - a standing trap, since both pairs exported the
// same `::Vertex` / `::ObjMaterial` type. This test asserts the set of
// interfaces with zero importers stays empty, so a module that loses its
// last importer is caught instead of silently becoming build-time-only
// weight.
TEST(BuildIntegrity, EveryModuleInterfaceIsImported)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path src_root = repo_root / "Src";
    ASSERT_TRUE(fs::exists(src_root)) << "missing " << src_root.string();

    const std::vector<ModuleInterface> modules = collect_module_interfaces(src_root);
    ASSERT_GT(modules.size(), 10U) << "found suspiciously few module interfaces under " << src_root.string()
                                   << " - the scan itself is likely broken";

    const std::vector<fs::path> search_roots = { src_root, repo_root / "Test" };
    const std::map<std::string, std::set<std::string>> importers = collect_module_importers(search_roots);

    // Modules with a justified reason to have no importer (e.g. a
    // consciously-rootless entry point module). Empty today - an entry here
    // requires a written reason, not just a failing test.
    const std::set<std::string> allowed_rootless_modules = {};

    std::vector<std::string> unimported;
    for (const auto &iface : modules) {
        if (allowed_rootless_modules.contains(iface.name)) { continue; }

        const auto it = importers.find(iface.name);
        const std::string declaring_path = iface.path.generic_string();
        const bool imported_elsewhere = it != importers.end()
          && std::any_of(it->second.begin(), it->second.end(), [&](const std::string &importer_path) {
                 return importer_path != declaring_path;
             });

        if (!imported_elsewhere) {
            unimported.push_back(iface.name + " (" + fs::relative(iface.path, repo_root).generic_string() + ")");
        }
    }

    EXPECT_TRUE(unimported.empty())
      << unimported.size()
      << " module interface(s) have no importer anywhere under Src/ or Test/ - either delete the dead module, "
         "or if it is deliberately rootless, add a justified entry to allowed_rootless_modules above: "
      << [&unimported] {
             std::string joined;
             for (const auto &entry : unimported) { joined += "\n  " + entry; }
             return joined;
         }();
}

namespace {

// file (relative to Src/GraphicsEngineVulkan/, forward slashes) : marker ->
// a deliberate exception to the rule below, with the reason it does not
// need ASSERT_VULKAN. This is not a way to silence a real gap - every entry
// must be justified, and every entry must be matched by a
// "// UNCHECKED_VULKAN_RESULT_OK: <marker>" trailing comment on the exempted
// line in the source (checked by VulkanCreationResultsAreChecked below), so
// an exemption cannot rot silently after the line it protects moves or is
// deleted.
struct AllowlistEntry
{
    std::string file;
    std::string marker;
};

// Both former entries here (ShaderHelper.cpp's already-fatal-abort-above and
// Clouds.cpp's noise-dispatch-skip-on-failure) became dead once the ".result"
// check in the ±8-line window was accepted as satisfying the gate on its own
// (see VulkanCreationResultsAreChecked below): each site's explicit
// `if (x.result != ...)` a few lines above its `.value` read already
// satisfies the gate without a special-case exemption.
const std::vector<AllowlistEntry> kCheckedResultAllowlist = {};

const std::string kUncheckedResultMarkerPrefix = "UNCHECKED_VULKAN_RESULT_OK: ";

// Returns the index of the kCheckedResultAllowlist entry whose marker is
// present as a trailing "// UNCHECKED_VULKAN_RESULT_OK: <marker>" comment on
// `line`, or -1 if none matches. Anchoring on the marker text rather than the
// line number means an unrelated edit above the exempted line cannot turn a
// live exemption into a false positive (or, worse, a silently-wrong one).
int allowlisted_result_check_index(const std::string &relative_file, const std::string &line)
{
    for (std::size_t idx = 0; idx < kCheckedResultAllowlist.size(); ++idx) {
        const AllowlistEntry &entry = kCheckedResultAllowlist[idx];
        if (entry.file != relative_file) { continue; }
        if (line.find(kUncheckedResultMarkerPrefix + entry.marker) != std::string::npos) {
            return static_cast<int>(idx);
        }
    }
    return -1;
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

// Same shape as looks_like_creation_call, keyed on the query verbs instead
// of the creation/allocation ones. `enumeratePhysicalDevices`,
// `getSurfaceCapabilitiesKHR` and friends return a vk::ResultValue exactly
// like a create*/allocate* call does, but the naming-based gate above never
// saw them - every surface and enumeration query silently slipped past it.
bool looks_like_query_call(const std::string &line)
{
    static const std::vector<std::string> keywords = { "get", "Get", "enumerate", "Enumerate" };
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
    // Self-verifying floor: assert the widened matcher actually fires before
    // relying on it below, so a future edit that neuters it fails loudly
    // instead of quietly reporting zero violations forever.
    EXPECT_TRUE(looks_like_query_call("  auto x = d.getSurfaceFormatsKHR(*s).value;"));
    EXPECT_FALSE(looks_like_query_call("  auto x = getter(y).value;"));

    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path engine_root = repo_root / "Src" / "GraphicsEngineVulkan";
    ASSERT_TRUE(fs::exists(engine_root)) << "missing " << engine_root.string();

    constexpr int kWindow = 8;
    std::vector<std::string> violations;
    std::vector<bool> allowlist_entry_matched(kCheckedResultAllowlist.size(), false);
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
                if (w <= i && (looks_like_creation_call(lines[w]) || looks_like_query_call(lines[w]))) {
                    triggered = true;
                }
                if (lines[w].find("ASSERT_VULKAN") != std::string::npos) { asserted = true; }
                // A query (unlike a creation/allocation call) is legitimately
                // handled with an explicit `if (x.result != ...)` rather than
                // an abort. Exclude the .value line itself so an unrelated
                // ".result" mention there cannot retroactively satisfy a
                // check that was never actually performed.
                if (w != i && lines[w].find(".result") != std::string::npos) { asserted = true; }
            }
            if (!triggered || asserted) { continue; }

            const std::string relative_file = fs::relative(path, engine_root).generic_string();
            const int allowlist_index = allowlisted_result_check_index(relative_file, lines[i]);
            if (allowlist_index >= 0) {
                allowlist_entry_matched[static_cast<std::size_t>(allowlist_index)] = true;
                continue;
            }

            violations.push_back(relative_file + ":" + std::to_string(i + 1) + ": " + lines[i]);
        }
    }

    EXPECT_TRUE(violations.empty())
      << violations.size()
      << " Vulkan creation/allocation/query result(s) read via .value with no ASSERT_VULKAN and no "
         "\".result\" check nearby (exceptions are disabled project-wide, so a failed call must either "
         "abort via ASSERT_VULKAN or be handled explicitly via an `if (x.result != ...)` check rather "
         "than continue with a silently default/empty value; or, for a deliberate exception, add a "
         "\"// UNCHECKED_VULKAN_RESULT_OK: <marker>\" comment on the line and a justified entry "
         "to kCheckedResultAllowlist above):"
      << [&violations] {
             std::string joined;
             for (const auto &entry : violations) { joined += "\n  " + entry; }
             return joined;
         }();

    std::vector<std::string> dead_exemptions;
    for (std::size_t idx = 0; idx < kCheckedResultAllowlist.size(); ++idx) {
        if (!allowlist_entry_matched[idx]) {
            dead_exemptions.push_back(kCheckedResultAllowlist[idx].file + " (" + kCheckedResultAllowlist[idx].marker + ")");
        }
    }

    EXPECT_TRUE(dead_exemptions.empty())
      << dead_exemptions.size()
      << " kCheckedResultAllowlist entr(y/ies) matched no \"// UNCHECKED_VULKAN_RESULT_OK: <marker>\" "
         "comment in the source - the exemption is dead and must be deleted:"
      << [&dead_exemptions] {
             std::string joined;
             for (const auto &entry : dead_exemptions) { joined += "\n  " + entry; }
             return joined;
         }();
}

// docs/gpu-golden-testing.md's golden-suite counts have already had to be
// corrected twice by hand (commits 1cd6b8b5, e2767bb1), and a planner batch
// once found the doc claiming 21 tests when the suite held 28. Pins the
// doc's `<!-- golden-counts: ... -->` marker against a pure file-I/O count of
// TEST(GoldenRender, ...) / TEST(Integration, ...) definitions, following the
// same "parse two sources, compare, fail with both numbers" pattern this
// suite uses throughout. Must never run the golden tests themselves to count them
// - they need a GPU the CI container does not have.
TEST(BuildIntegrity, GoldenTestCountsInDocsMatchTheSuite)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path doc_path = repo_root / "docs" / "gpu-golden-testing.md";
    if (!fs::exists(doc_path)) {
        GTEST_SKIP() << "could not open " << doc_path.string() << " - not running from the repo root?";
    }

    const auto marker = parse_golden_counts_marker(doc_path);
    ASSERT_TRUE(marker.has_value())
      << doc_path.string()
      << " is missing its '<!-- golden-counts: defined=N runnable=N integration=N total=N -->' marker line, or "
         "one of its four fields - a deleted marker must fail this test, not silently pass";

    const fs::path tests_dir = repo_root / "Test" / "commit" / "VulkanEngine";
    const std::vector<std::string> golden_tests = collect_suite_test_names(tests_dir, "GoldenRender");
    const std::vector<std::string> integration_tests = collect_suite_test_names(tests_dir, "Integration");

    const int counted_defined = static_cast<int>(golden_tests.size());
    const int counted_runnable = static_cast<int>(std::count_if(golden_tests.begin(), golden_tests.end(),
      [](const std::string &name) { return !name.starts_with("DISABLED_"); }));
    const int counted_integration = static_cast<int>(integration_tests.size());

    EXPECT_EQ(marker->defined, counted_defined)
      << doc_path.string() << "'s golden-counts marker says defined=" << marker->defined << " but "
      << tests_dir.string() << " has " << counted_defined << " TEST(GoldenRender, ...) definitions";
    EXPECT_EQ(marker->runnable, counted_runnable)
      << doc_path.string() << "'s golden-counts marker says runnable=" << marker->runnable << " but "
      << counted_runnable << " of " << counted_defined
      << " TEST(GoldenRender, ...) definitions do not start with DISABLED_";
    EXPECT_EQ(marker->integration, counted_integration)
      << doc_path.string() << "'s golden-counts marker says integration=" << marker->integration << " but "
      << tests_dir.string() << " has " << counted_integration << " TEST(Integration, ...) definitions";
    EXPECT_EQ(marker->runnable + marker->integration, marker->total)
      << doc_path.string() << "'s golden-counts marker is internally inconsistent: runnable(" << marker->runnable
      << ") + integration(" << marker->integration << ") != total(" << marker->total << ")";
}

// Parses the X, Y, Z triple out of the first "[numthreads(X, Y, Z)]" in
// `path`. Returns nullopt if the attribute is not found, so callers can tell
// "found and mismatched" apart from "a renamed/removed attribute silently
// matched zero times".
std::optional<std::array<int, 3>> parse_numthreads(const fs::path &path)
{
    std::ifstream file(path);
    if (!file) { return std::nullopt; }

    static const std::regex kNumThreadsPattern(R"(\[numthreads\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)\])");

    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::smatch match;
    if (!std::regex_search(contents, match, kNumThreadsPattern)) { return std::nullopt; }

    return std::array<int, 3>{ std::stoi(match[1].str()), std::stoi(match[2].str()), std::stoi(match[3].str()) };
}

// Clouds.cpp dispatches the noise and cloud compute passes using
// kNoiseWorkgroupSize/kCloudWorkgroupSize (CloudDispatch.hpp) to size the
// thread-group grid. Those constants have no compiler-enforced link to the
// [numthreads(...)] attribute the corresponding Slang kernel actually
// declares - halving noise.slang's workgroup to (4,4,4) without touching
// CloudDispatch.hpp would leave 7/8 of the noise volume undefined, and
// nothing short of a GPU golden test would notice. Mirrors
// HostAndShaderSharedConstantsAgree's "parse the shader text, compare
// against the compiled host value" shape.
TEST(BuildIntegrity, CloudDispatchGridsMatchTheShaderWorkgroupSizes)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path noise_path = repo_root / "Resources" / "ShadersSlang" / "compute" / "noise.slang";
    const fs::path clouds_path = repo_root / "Resources" / "ShadersSlang" / "compute" / "clouds.slang";

    const auto noise_threads = parse_numthreads(noise_path);
    ASSERT_TRUE(noise_threads.has_value()) << "no [numthreads(...)] attribute found in " << noise_path.string();
    EXPECT_EQ((*noise_threads)[0], static_cast<int>(Kataglyphis::kNoiseWorkgroupSize))
      << noise_path.string() << "'s [numthreads(" << (*noise_threads)[0] << ", " << (*noise_threads)[1] << ", "
      << (*noise_threads)[2] << ")] X does not match CloudDispatch.hpp's kNoiseWorkgroupSize ("
      << Kataglyphis::kNoiseWorkgroupSize << ')';
    EXPECT_EQ((*noise_threads)[1], static_cast<int>(Kataglyphis::kNoiseWorkgroupSize))
      << noise_path.string() << "'s [numthreads(" << (*noise_threads)[0] << ", " << (*noise_threads)[1] << ", "
      << (*noise_threads)[2] << ")] Y does not match CloudDispatch.hpp's kNoiseWorkgroupSize ("
      << Kataglyphis::kNoiseWorkgroupSize << ')';
    EXPECT_EQ((*noise_threads)[2], static_cast<int>(Kataglyphis::kNoiseWorkgroupSize))
      << noise_path.string() << "'s [numthreads(" << (*noise_threads)[0] << ", " << (*noise_threads)[1] << ", "
      << (*noise_threads)[2] << ")] Z does not match CloudDispatch.hpp's kNoiseWorkgroupSize ("
      << Kataglyphis::kNoiseWorkgroupSize << ')';

    const auto cloud_threads = parse_numthreads(clouds_path);
    ASSERT_TRUE(cloud_threads.has_value()) << "no [numthreads(...)] attribute found in " << clouds_path.string();
    EXPECT_EQ((*cloud_threads)[0], static_cast<int>(Kataglyphis::kCloudWorkgroupSize))
      << clouds_path.string() << "'s [numthreads(" << (*cloud_threads)[0] << ", " << (*cloud_threads)[1] << ", "
      << (*cloud_threads)[2] << ")] X does not match CloudDispatch.hpp's kCloudWorkgroupSize ("
      << Kataglyphis::kCloudWorkgroupSize << ')';
    EXPECT_EQ((*cloud_threads)[1], static_cast<int>(Kataglyphis::kCloudWorkgroupSize))
      << clouds_path.string() << "'s [numthreads(" << (*cloud_threads)[0] << ", " << (*cloud_threads)[1] << ", "
      << (*cloud_threads)[2] << ")] Y does not match CloudDispatch.hpp's kCloudWorkgroupSize ("
      << Kataglyphis::kCloudWorkgroupSize << ')';
    EXPECT_EQ((*cloud_threads)[2], 1) << clouds_path.string() << "'s [numthreads(" << (*cloud_threads)[0] << ", "
                                       << (*cloud_threads)[1] << ", " << (*cloud_threads)[2] << ")] Z is not 1";
}

// PathTracing.cpp dispatches the path tracing compute pass using
// kPathTracingWorkgroupSizeX/Y (PathTracingDispatch.hpp) to size the
// thread-group grid. Those constants have no compiler-enforced link to the
// [numthreads(...)] attribute path_tracing.slang actually declares - this
// already went wrong once (found 2026-07-31 at (16, 8) against a shader
// compiled at (8, 8), under-covering the image width by 2x every frame).
// Mirrors CloudDispatchGridsMatchTheShaderWorkgroupSizes's shape.
TEST(BuildIntegrity, PathTracingDispatchMatchesTheShaderWorkgroupSize)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path path_tracing_path =
      repo_root / "Resources" / "ShadersSlang" / "path_tracing" / "path_tracing.slang";

    const auto path_tracing_threads = parse_numthreads(path_tracing_path);
    ASSERT_TRUE(path_tracing_threads.has_value())
      << "no [numthreads(...)] attribute found in " << path_tracing_path.string();
    EXPECT_EQ((*path_tracing_threads)[0], static_cast<int>(Kataglyphis::kPathTracingWorkgroupSizeX))
      << path_tracing_path.string() << "'s [numthreads(" << (*path_tracing_threads)[0] << ", "
      << (*path_tracing_threads)[1] << ", " << (*path_tracing_threads)[2]
      << ")] X does not match PathTracingDispatch.hpp's kPathTracingWorkgroupSizeX ("
      << Kataglyphis::kPathTracingWorkgroupSizeX << ')';
    EXPECT_EQ((*path_tracing_threads)[1], static_cast<int>(Kataglyphis::kPathTracingWorkgroupSizeY))
      << path_tracing_path.string() << "'s [numthreads(" << (*path_tracing_threads)[0] << ", "
      << (*path_tracing_threads)[1] << ", " << (*path_tracing_threads)[2]
      << ")] Y does not match PathTracingDispatch.hpp's kPathTracingWorkgroupSizeY ("
      << Kataglyphis::kPathTracingWorkgroupSizeY << ')';
    EXPECT_EQ((*path_tracing_threads)[2], 1)
      << path_tracing_path.string() << "'s [numthreads(" << (*path_tracing_threads)[0] << ", "
      << (*path_tracing_threads)[1] << ", " << (*path_tracing_threads)[2] << ")] Z is not 1";
}

// A stray NUL or other C0 control byte inside a source file makes
// grep/ripgrep treat the whole file as binary ("binary file matches" instead
// of printing the line), which silently excludes every call site in it from
// this project's grep-based planner/executor workflow. SceneConfig.cpp's
// KATAGLYPHIS_MODEL_OVERRIDE guard carried a literal NUL byte instead of the
// two-character '\0' escape for exactly this reason until this test caught
// it. UTF-8 continuation bytes (>= 0x80) and BOMs are not control bytes and
// are intentionally not flagged.
TEST(BuildIntegrity, ProjectSourcesContainNoStrayControlBytes)
{
    const fs::path repo_root = find_repo_root();
    if (repo_root.empty()) { GTEST_SKIP() << "could not locate the repository root"; }

    std::vector<std::string> offenders;

    auto scan_file = [&](const fs::path &path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) { return; }
        const std::string relative = fs::relative(path, repo_root).generic_string();
        char raw_byte;
        std::size_t offset = 0;
        while (file.get(raw_byte)) {
            const auto byte = static_cast<unsigned char>(raw_byte);
            const bool is_stray_control_byte = byte < 0x20 && byte != '\t' && byte != '\n' && byte != '\r';
            if (is_stray_control_byte) {
                offenders.push_back(relative + " (byte offset " + std::to_string(offset) + ")");
            }
            ++offset;
        }
    };

    const std::vector<fs::path> code_roots = { repo_root / "Src", repo_root / "Test" };
    for (const auto &root : code_roots) {
        std::error_code error;
        if (!fs::exists(root, error)) { continue; }

        for (fs::recursive_directory_iterator it(root, error), end; it != end; it.increment(error)) {
            if (error) { break; }
            if (!it->is_regular_file(error)) { continue; }
            const auto extension = it->path().extension();
            if (extension != ".cpp" && extension != ".hpp" && extension != ".ixx") { continue; }
            scan_file(it->path());
        }
    }

    const fs::path slang_root = repo_root / "Resources" / "ShadersSlang";
    const std::string slang_build_prefix = (slang_root / "build").generic_string() + "/";
    std::error_code error;
    if (fs::exists(slang_root, error)) {
        for (fs::recursive_directory_iterator it(slang_root, error), end; it != end; it.increment(error)) {
            if (error) { break; }
            const fs::path &path = it->path();
            if (path.generic_string().rfind(slang_build_prefix, 0) == 0) { continue; }// compiled output tree
            if (!it->is_regular_file(error) || path.extension() != ".slang") { continue; }
            scan_file(path);
        }
    }

    EXPECT_TRUE(offenders.empty())
      << offenders.size()
      << " project source file(s) contain a stray control byte (NUL or a C0 control other than tab/LF/CR), "
         "which makes grep/ripgrep treat the file as binary and silently excludes it from every text search "
         "this project's tooling relies on: "
      << [&offenders] {
             std::string joined;
             for (const auto &entry : offenders) { joined += "\n  " + entry; }
             return joined;
         }();
}

// Every layout contract in this repo used to be a hand-copied number: the
// SceneUBO_std140.cascadeSplits drift (fixed by SceneUboLayoutUnit's pad
// member) survived because nothing compared the host offsetof()s against
// what slangc actually emitted. This test makes the compiled SPIR-V the
// source of truth: it parses every committed .spv's OpName/OpMemberName/
// OpMemberDecorate triples and checks each contracted struct's members land
// at the same byte offset the host struct computes.
TEST(BuildIntegrity, SharedStructOffsetsMatchTheCompiledSpirv)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path spirv_root = repo_root / "Resources" / "ShadersSlang" / "build" / "spirv";
    ASSERT_TRUE(fs::exists(spirv_root)) << "missing " << spirv_root.string();

    std::map<std::string, std::map<std::string, uint32_t>> compiled;// union across every .spv
    std::error_code error;
    for (fs::recursive_directory_iterator it(spirv_root, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        if (!it->is_regular_file(error) || it->path().extension() != ".spv") { continue; }

        const auto parsed = parse_spirv_member_offsets(it->path());
        if (!parsed.has_value()) { continue; }// unreadable/invalid SPIR-V - CompiledShadersAreNotOlderThan* catches that

        for (const auto &[struct_name, members] : *parsed) {
            for (const auto &[member_name, offset] : members) { compiled[struct_name][member_name] = offset; }
        }
    }

    const auto contracts = build_shared_struct_offset_contracts();
    std::vector<std::string> not_found_in_any_spv;
    std::vector<std::string> offset_mismatches;

    for (const auto &contract : contracts) {
        const auto struct_it = compiled.find(contract.spirv_name);
        if (struct_it == compiled.end()) {
            not_found_in_any_spv.push_back(contract.spirv_name);
            continue;
        }

        for (const auto &[member_name, host_offset] : contract.member_offsets) {
            const auto member_it = struct_it->second.find(member_name);
            if (member_it == struct_it->second.end()) {
                offset_mismatches.push_back(
                  contract.spirv_name + "." + member_name + ": not emitted as a member by any compiled .spv");
                continue;
            }
            if (member_it->second != host_offset) {
                offset_mismatches.push_back(contract.spirv_name + "." + member_name + ": compiled SPIR-V offset "
                  + std::to_string(member_it->second) + " != host offsetof " + std::to_string(host_offset));
            }
        }
    }

    EXPECT_TRUE(not_found_in_any_spv.empty())
      << not_found_in_any_spv.size()
      << " struct(s) expected in the compiled SPIR-V were not emitted by ANY .spv under " << spirv_root.string()
      << " - renamed or deleted shader struct: "
      << [&not_found_in_any_spv] {
             std::string joined;
             for (const auto &entry : not_found_in_any_spv) { joined += "\n  " + entry; }
             return joined;
         }();

    EXPECT_TRUE(offset_mismatches.empty())
      << offset_mismatches.size() << " host/SPIR-V struct-offset mismatch(es):"
      << [&offset_mismatches] {
             std::string joined;
             for (const auto &entry : offset_mismatches) { joined += "\n  " + entry; }
             return joined;
         }();
}

// FileReader.ixx spells out the rule this test enforces: the error_code
// overload of std::filesystem's query functions is REQUIRED, not stylistic,
// because exceptions are disabled project-wide (-fno-exceptions/EHs-) and the
// throwing overloads therefore std::terminate the whole process on any OS
// error the query reports - most commonly permission denied. scanAvailableModels
// used to walk a user-populated directory (Resources/Models, recursively)
// with the throwing range-for form; a permission-denied subdirectory or a
// junction loop was enough to take the engine down instead of skipping the
// entry. This scans every .cpp/.ixx/.hpp under Src/ for a call to one of the
// throwing-capable functions and fails any call site with no error_code
// argument in sight. A deliberate exception can carry a
// "// NO_EC_OK: <reason>" trailing comment.
TEST(BuildIntegrity, EngineSourcesUseNonThrowingFilesystemOverloads)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path src_root = repo_root / "Src";
    ASSERT_TRUE(fs::exists(src_root)) << "missing " << src_root.string();

    static const std::vector<std::string> kWatchedCalls = {
        "filesystem::exists(",
        "filesystem::current_path(",
        "filesystem::relative(",
        "filesystem::create_directories(",
        "directory_iterator(",// also matches recursive_directory_iterator(
    };
    static const std::string kNoEcMarker = "NO_EC_OK:";

    // Not a full parser - a plain substring search for the naming idioms this
    // codebase actually uses for an std::error_code out-parameter (ec,
    // *_ec, *_error, error_code, or the FileReader.ixx "ignored" idiom for a
    // deliberately-discarded one).
    auto carries_error_code_token = [](const std::string &text) {
        return text.find("ec") != std::string::npos || text.find("error") != std::string::npos
          || text.find("ignored") != std::string::npos;
    };

    std::vector<std::string> violations;
    std::error_code error;
    for (fs::recursive_directory_iterator it(src_root, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        const fs::path &path = it->path();
        if (!it->is_regular_file(error)) { continue; }
        const auto extension = path.extension();
        if (extension != ".cpp" && extension != ".ixx" && extension != ".hpp") { continue; }

        std::ifstream file(path);
        if (!file) { continue; }
        std::vector<std::string> lines;
        std::string raw_line;
        while (std::getline(file, raw_line)) { lines.push_back(raw_line); }

        constexpr int kLookaheadLines = 2;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            for (const auto &watched : kWatchedCalls) {
                if (lines[i].find(watched) == std::string::npos) { continue; }
                if (lines[i].find(kNoEcMarker) != std::string::npos) { continue; }

                std::string window = lines[i];
                for (int extra = 1; extra <= kLookaheadLines && i + static_cast<std::size_t>(extra) < lines.size();
                     ++extra) {
                    window += lines[i + static_cast<std::size_t>(extra)];
                }

                if (!carries_error_code_token(window)) {
                    violations.push_back(
                      fs::relative(path, repo_root).generic_string() + ":" + std::to_string(i + 1) + ": " + lines[i]);
                }
            }
        }
    }

    EXPECT_TRUE(violations.empty())
      << violations.size()
      << " std::filesystem call(s) under Src/ use a throwing overload (no error_code argument found nearby) - "
         "exceptions are disabled project-wide (-fno-exceptions/EHs-), so this aborts the process instead of "
         "returning an error; pass the error_code overload (see FileReader.ixx), or for a deliberate exception "
         "add a \"// NO_EC_OK: <reason>\" trailing comment on the line:"
      << [&violations] {
             std::string joined;
             for (const auto &entry : violations) { joined += "\n  " + entry; }
             return joined;
         }();
}

TEST(BuildIntegrity, EngineSourcesDoNotLogRawVulkanHandles)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path src_root = repo_root / "Src";
    ASSERT_TRUE(fs::exists(src_root)) << "missing " << src_root.string();

    std::vector<std::string> violations;
    std::error_code error;
    for (fs::recursive_directory_iterator it(src_root, error), end; it != end; it.increment(error)) {
        if (error) { break; }
        const fs::path &path = it->path();
        if (!it->is_regular_file(error)) { continue; }
        const auto extension = path.extension();
        if (extension != ".cpp" && extension != ".ixx") { continue; }

        std::ifstream file(path);
        if (!file) { continue; }
        std::string line;
        std::size_t line_number = 0;
        while (std::getline(file, line)) {
            ++line_number;
            if (line.find("spdlog::") == std::string::npos) { continue; }
            const bool has_vk_cast_to_uint64 = line.find("(uint64_t)(Vk") != std::string::npos;
            const bool has_hex_format_with_vk_cast = line.find("0x{:x}") != std::string::npos && line.find("Vk") != std::string::npos;
            if (has_vk_cast_to_uint64 || has_hex_format_with_vk_cast) {
                violations.push_back(
                  fs::relative(path, repo_root).generic_string() + ":" + std::to_string(line_number) + ": " + line);
            }
        }
    }

    EXPECT_TRUE(violations.empty())
      << violations.size()
      << " spdlog call(s) under Src/ log a raw Vulkan handle - these are noise (the validation layers and Vulkan "
         "debug labels already give a better diagnostic) and read as intentional instrumentation; delete them "
         "instead of demoting to spdlog::debug:"
      << [&violations] {
             std::string joined;
             for (const auto &entry : violations) { joined += "\n  " + entry; }
             return joined;
         }();
}
