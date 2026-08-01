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

// Slang source (relative to Resources/ShadersSlang/) -> checked-in Rust-crate
// WGSL destination (relative to
// ExternalLib/Kataglyphis-RustProjectTemplate/crates/), mirroring $WgslMap in
// Scripts/Windows/compile-slang-shaders.ps1 (and its bash equivalent). Kept as
// a second hand-written copy, same as kSharedConstantNames below, rather than
// parsed out of the PowerShell array - a divergence here is exactly the drift
// this test exists to catch, so the pairs are deliberately independent of the
// script's own list. histogram.wgsl is deliberately excluded: it is
// hand-written, with no generating Slang source.
struct WgslMapping
{
    std::string slang_source;// relative to Resources/ShadersSlang/
    std::string crate_dir;   // relative to ExternalLib/Kataglyphis-RustProjectTemplate/crates/
    std::string wgsl_file;
};

const std::vector<WgslMapping> kWgslMap = {
    { "forward/forward.slang", "webgpu_renderer/src/shaders", "forward.wgsl" },
    { "sky/sky.slang", "webgpu_renderer/src/shaders", "sky.wgsl" },
    { "bloom/bloom.slang", "webgpu_renderer/src/shaders", "bloom.wgsl" },
    { "ssao/ssao.slang", "webgpu_renderer/src/shaders", "ssao.wgsl" },
    { "ibl/ibl.slang", "webgpu_renderer/src/shaders", "ibl.wgsl" },
    { "gpu_cull/gpu_cull.slang", "webgpu_renderer/src/shaders", "gpu_cull.wgsl" },
    { "tonemap/tonemap.slang", "webgpu_renderer/src/shaders", "tonemap.wgsl" },
    { "depth_resolve/depth_resolve.slang", "webgpu_renderer/src/shaders", "depth_resolve.wgsl" },
    { "occlusion_bbox/occlusion_bbox.slang", "webgpu_renderer/src/shaders", "occlusion_bbox.wgsl" },
    { "tex_quad/tex_quad.slang", "gui/src/shaders", "tex_quad.wgsl" },
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

    static const std::string kAnchor = "foreach (`$t in @(";
    static const std::string kCloser = "))";

    std::string line;
    while (std::getline(file, line)) {
        const std::size_t anchor_pos = line.find(kAnchor);
        if (anchor_pos == std::string::npos) { continue; }

        const std::size_t list_start = anchor_pos + kAnchor.size();
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

// Parses Scripts/Linux/compile-slang-shaders.sh's `MANIFEST=( ... )` bash
// array - each row is "file|entry|stage|targets", targets being a
// comma-separated list of 'spirv' and/or 'wgsl' - and returns the relative
// (to Resources/ShadersSlang/) .slang paths of every row whose targets
// include 'spirv'. That manifest is the single source of truth for which
// shader sources the C++/Vulkan engine actually compiles; a hand-written
// mirror of it would itself be the kind of drift this suite exists to catch.
// Anchored on the "MANIFEST=(" opener and the ")" that closes it, so the
// unrelated WGSL_MAP array further down the script cannot be picked up.
// Commented-out rows (leading '#') are skipped.
std::set<std::string> parse_vulkan_consumed_slang_sources(const fs::path &script_path)
{
    std::set<std::string> result;
    std::ifstream file(script_path);
    if (!file) { return result; }

    bool inside_manifest = false;
    std::string line;
    while (std::getline(file, line)) {
        if (!inside_manifest) {
            if (line.find("MANIFEST=(") != std::string::npos) { inside_manifest = true; }
            continue;
        }

        const std::size_t first_non_space = line.find_first_not_of(" \t");
        const std::string trimmed = first_non_space == std::string::npos ? std::string{} : line.substr(first_non_space);
        if (trimmed.starts_with(")")) { break; }// end of MANIFEST=( ... )
        if (trimmed.empty() || trimmed.starts_with("#")) { continue; }

        const std::size_t open_quote = line.find('"');
        if (open_quote == std::string::npos) { continue; }
        const std::size_t close_quote = line.find('"', open_quote + 1);
        if (close_quote == std::string::npos) { continue; }
        const std::string row = line.substr(open_quote + 1, close_quote - open_quote - 1);

        std::vector<std::string> fields;
        std::size_t pos = 0;
        while (true) {
            const std::size_t bar = row.find('|', pos);
            fields.push_back(row.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos));
            if (bar == std::string::npos) { break; }
            pos = bar + 1;
        }
        if (fields.size() != 4) { continue; }// not a "file|entry|stage|targets" row

        const std::string &file_field = fields[0];
        const std::string &targets_field = fields[3];
        if (targets_field.find("spirv") != std::string::npos) { result.insert(file_field); }
    }
    return result;
}

// Normalizes a manifest row's target list ('spirv', 'wgsl', or both, in
// either order) into a canonical comma-joined, sorted form so 'spirv,wgsl'
// and 'wgsl,spirv' compare equal.
std::string normalize_targets(std::vector<std::string> targets)
{
    std::sort(targets.begin(), targets.end());
    std::string joined;
    for (const auto &target : targets) {
        if (!joined.empty()) { joined += ','; }
        joined += target;
    }
    return joined;
}

// Parses $Manifest from compile-slang-shaders.ps1: an array of
// `@{ File = '...'; Entry = '...'; Stage = '...'; Targets = @('...', ...) }`
// hashtables. Returns each row normalized to "file|entry|stage|sorted
// targets", with '\' path separators converted to '/' so a row compares
// equal to its bash-side counterpart. Anchored on the "$Manifest = @(" opener
// and the ")" that closes it. Commented-out rows (leading '#') are skipped -
// compile-slang-shaders.ps1 carries one (the hand-written histogram.wgsl
// row), and a naive parse would count it against a manifest pair that
// actually agrees.
std::set<std::string> parse_powershell_manifest_rows(const fs::path &script_path)
{
    std::set<std::string> result;
    std::ifstream file(script_path);
    if (!file) { return result; }

    bool inside_manifest = false;
    std::string line;
    while (std::getline(file, line)) {
        if (!inside_manifest) {
            if (line.find("$Manifest = @(") != std::string::npos) { inside_manifest = true; }
            continue;
        }

        const std::size_t first_non_space = line.find_first_not_of(" \t");
        const std::string trimmed = first_non_space == std::string::npos ? std::string{} : line.substr(first_non_space);
        if (trimmed.starts_with(")")) { break; }// end of $Manifest = @( ... )
        if (trimmed.empty() || trimmed.starts_with("#")) { continue; }

        const std::size_t file_key = line.find("File = '");
        if (file_key == std::string::npos) { continue; }// not a manifest row
        const std::size_t file_start = file_key + std::string("File = '").size();
        const std::size_t file_end = line.find('\'', file_start);
        if (file_end == std::string::npos) { continue; }
        std::string manifest_file = line.substr(file_start, file_end - file_start);
        std::replace(manifest_file.begin(), manifest_file.end(), '\\', '/');

        const std::size_t entry_key = line.find("Entry = '", file_end);
        if (entry_key == std::string::npos) { continue; }
        const std::size_t entry_start = entry_key + std::string("Entry = '").size();
        const std::size_t entry_end = line.find('\'', entry_start);
        if (entry_end == std::string::npos) { continue; }
        const std::string entry = line.substr(entry_start, entry_end - entry_start);

        const std::size_t stage_key = line.find("Stage = '", entry_end);
        if (stage_key == std::string::npos) { continue; }
        const std::size_t stage_start = stage_key + std::string("Stage = '").size();
        const std::size_t stage_end = line.find('\'', stage_start);
        if (stage_end == std::string::npos) { continue; }
        const std::string stage = line.substr(stage_start, stage_end - stage_start);

        const std::size_t targets_key = line.find("Targets = @(", stage_end);
        if (targets_key == std::string::npos) { continue; }
        const std::size_t targets_list_start = targets_key + std::string("Targets = @(").size();
        const std::size_t targets_list_end = line.find(')', targets_list_start);
        if (targets_list_end == std::string::npos) { continue; }
        const std::string targets_list = line.substr(targets_list_start, targets_list_end - targets_list_start);

        std::vector<std::string> targets;
        std::size_t pos = 0;
        while (pos < targets_list.size()) {
            const std::size_t open_quote = targets_list.find('\'', pos);
            if (open_quote == std::string::npos) { break; }
            const std::size_t close_quote = targets_list.find('\'', open_quote + 1);
            if (close_quote == std::string::npos) { break; }
            targets.push_back(targets_list.substr(open_quote + 1, close_quote - open_quote - 1));
            pos = close_quote + 1;
        }
        if (targets.empty()) { continue; }

        result.insert(manifest_file + '|' + entry + '|' + stage + '|' + normalize_targets(targets));
    }
    return result;
}

// Parses MANIFEST from compile-slang-shaders.sh: a bash array of
// "file|entry|stage|targets" strings, targets a comma-separated list of
// 'spirv' and/or 'wgsl'. Returns each row normalized identically to
// parse_powershell_manifest_rows, so the two sets compare equal regardless of
// path-separator or target-order differences. Anchored on the "MANIFEST=("
// opener and the ")" that closes it, so the unrelated WGSL_MAP array further
// down the script cannot be picked up. Commented-out rows (leading '#') are
// skipped.
std::set<std::string> parse_bash_manifest_rows(const fs::path &script_path)
{
    std::set<std::string> result;
    std::ifstream file(script_path);
    if (!file) { return result; }

    bool inside_manifest = false;
    std::string line;
    while (std::getline(file, line)) {
        if (!inside_manifest) {
            if (line.find("MANIFEST=(") != std::string::npos) { inside_manifest = true; }
            continue;
        }

        const std::size_t first_non_space = line.find_first_not_of(" \t");
        const std::string trimmed = first_non_space == std::string::npos ? std::string{} : line.substr(first_non_space);
        if (trimmed.starts_with(")")) { break; }// end of MANIFEST=( ... )
        if (trimmed.empty() || trimmed.starts_with("#")) { continue; }

        const std::size_t open_quote = line.find('"');
        if (open_quote == std::string::npos) { continue; }
        const std::size_t close_quote = line.find('"', open_quote + 1);
        if (close_quote == std::string::npos) { continue; }
        const std::string row = line.substr(open_quote + 1, close_quote - open_quote - 1);

        std::vector<std::string> fields;
        std::size_t pos = 0;
        while (true) {
            const std::size_t bar = row.find('|', pos);
            fields.push_back(row.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos));
            if (bar == std::string::npos) { break; }
            pos = bar + 1;
        }
        if (fields.size() != 4) { continue; }// not a "file|entry|stage|targets" row

        std::vector<std::string> targets;
        std::size_t target_pos = 0;
        const std::string &targets_field = fields[3];
        while (true) {
            const std::size_t comma = targets_field.find(',', target_pos);
            targets.push_back(
              targets_field.substr(target_pos, comma == std::string::npos ? std::string::npos : comma - target_pos));
            if (comma == std::string::npos) { break; }
            target_pos = comma + 1;
        }

        result.insert(fields[0] + '|' + fields[1] + '|' + fields[2] + '|' + normalize_targets(targets));
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

// Scripts/Windows/compile-slang-shaders.ps1's $Manifest and
// Scripts/Linux/compile-slang-shaders.sh's MANIFEST are two hand-maintained
// copies of the same "which Slang source compiles to which target(s)" table -
// one PowerShell array, one bash array - and nothing pins them together.
// SlangWgslPatchTablesAgree above only compares the WGSL post-emit patch
// tables, which are gated on the manifests they patch, not on the manifests'
// own row sets: a shader added to one script's manifest and forgotten on the
// other is never caught there, and is never compiled on that platform - while
// the stale checked-in artifact keeps every other staleness gate green,
// because the source it is compared against was never recompiled there. This
// test parses both manifests as text and asserts they agree on every row.
TEST(BuildIntegrity, SlangCompileManifestsAgree)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path windows_script = repo_root / "Scripts" / "Windows" / "compile-slang-shaders.ps1";
    const fs::path linux_script = repo_root / "Scripts" / "Linux" / "compile-slang-shaders.sh";

    const auto windows_rows = parse_powershell_manifest_rows(windows_script);
    const auto linux_rows = parse_bash_manifest_rows(linux_script);

    ASSERT_FALSE(windows_rows.empty())
      << "parsed zero rows out of $Manifest in " << windows_script.string()
      << " - the anchor text ('$Manifest = @(') may have changed";
    ASSERT_FALSE(linux_rows.empty()) << "parsed zero rows out of MANIFEST=( ... ) in " << linux_script.string()
                                     << " - the anchor text ('MANIFEST=(') may have changed";

    std::vector<std::string> windows_only;
    for (const auto &row : windows_rows) {
        if (!linux_rows.contains(row)) { windows_only.push_back(row); }
    }
    EXPECT_TRUE(windows_only.empty())
      << windows_only.size() << " row(s) in " << windows_script.string() << "'s $Manifest have no matching row in "
      << linux_script.string() << "'s MANIFEST:" << [&windows_only] {
             std::string joined;
             for (const auto &entry : windows_only) { joined += "\n  " + entry; }
             return joined;
         }();

    std::vector<std::string> linux_only_rows;
    for (const auto &row : linux_rows) {
        if (!windows_rows.contains(row)) { linux_only_rows.push_back(row); }
    }
    EXPECT_TRUE(linux_only_rows.empty())
      << linux_only_rows.size() << " row(s) in " << linux_script.string()
      << "'s MANIFEST have no matching row in " << windows_script.string() << "'s $Manifest:" << [&linux_only_rows] {
             std::string joined;
             for (const auto &entry : linux_only_rows) { joined += "\n  " + entry; }
             return joined;
         }();
}

// CompiledShadersAreNotOlderThanTheirSources guards the SPIR-V artifacts; the
// checked-in Rust-crate WGSL artifacts (kWgslMap above) have no equivalent
// guard, and they live two directories away
// (ExternalLib/Kataglyphis-RustProjectTemplate/crates/.../shaders) from the
// Slang source that generates them. A regenerate that drops one of the
// hand-applied depth-texture patches (see SlangWgslPatchTablesAgree above),
// or a .slang edit that never gets propagated, is silent today. This walks
// kWgslMap and asserts each checked-in .wgsl is not older than its source.
TEST(BuildIntegrity, CheckedInWgslIsNotOlderThanItsSlangSource)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path slang_root = repo_root / "Resources" / "ShadersSlang";
    const fs::path crates_root = repo_root / "ExternalLib" / "Kataglyphis-RustProjectTemplate" / "crates";

    std::vector<std::string> stale;
    int checked = 0;
    for (const auto &mapping : kWgslMap) {
        const fs::path source = slang_root / mapping.slang_source;
        ASSERT_TRUE(fs::exists(source)) << "Slang source mapped by kWgslMap is missing: " << source.string();

        const fs::path dest = crates_root / mapping.crate_dir / mapping.wgsl_file;
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
// destination from kWgslMap is therefore always a hand-edit made directly on
// the generated file, with a regenerate's expiry date on it: the next
// compile-slang-shaders run silently drops it. Catch it here instead.
TEST(BuildIntegrity, CheckedInWgslHasNoHandEdits)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path crates_root = repo_root / "ExternalLib" / "Kataglyphis-RustProjectTemplate" / "crates";

    std::vector<std::string> hand_edits;
    int checked = 0;
    for (const auto &mapping : kWgslMap) {
        const fs::path dest = crates_root / mapping.crate_dir / mapping.wgsl_file;
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

    const fs::path manifest_script = repo_root / "Scripts" / "Linux" / "compile-slang-shaders.sh";
    const auto vulkan_consumed_sources = parse_vulkan_consumed_slang_sources(manifest_script);
    ASSERT_FALSE(vulkan_consumed_sources.empty())
      << "no spirv-targeted rows parsed from " << manifest_script.string() << " - manifest format changed?";

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

const std::vector<AllowlistEntry> kCheckedResultAllowlist = {
    // Already fatal: spdlog::critical + std::abort() a few lines above,
    // just not spelled with the ASSERT_VULKAN macro (the message embeds the
    // numeric vk::Result, which the macro's fixed string cannot).
    { "vulkan_base/ShaderHelper.cpp", "already-fatal-abort-above" },
    // Deliberately non-fatal: cloud noise generation is a one-shot compute
    // dispatch. If the transient command pool fails to create, the dispatch
    // is skipped (logged) rather than aborting the whole renderer over an
    // atmospheric effect.
    { "scene/atmospheric_effects/clouds/Clouds.cpp", "noise-dispatch-skip-on-failure" },
};

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
                if (w <= i && looks_like_creation_call(lines[w])) { triggered = true; }
                if (lines[w].find("ASSERT_VULKAN") != std::string::npos) { asserted = true; }
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
      << " Vulkan creation/allocation result(s) read via .value with no ASSERT_VULKAN nearby "
         "(exceptions are disabled project-wide, so a failed creation call must abort rather than "
         "continue into a null-handle dereference; add ASSERT_VULKAN, or for a deliberate exception "
         "add a \"// UNCHECKED_VULKAN_RESULT_OK: <marker>\" comment on the line and a justified entry "
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
