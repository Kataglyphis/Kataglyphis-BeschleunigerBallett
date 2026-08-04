#pragma once

// Every commit-test suite that gates on source-file content (buildIntegritySuite,
// renderPassCreateHelperSuite, sceneAsyncLoadSuite, ...) needs the same two
// things: a way to find the repo root from wherever the test binary's working
// directory happens to be, and a way to slurp a whole file into a string to
// scan it. Three suites each grew their own copy of both - one of the copies
// (renderPassCreateHelperSuite's read_file) even had a different failure
// contract (empty string, unchecked) than the other two (std::optional,
// checked). This header is the one place those helpers live now.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

namespace Kataglyphis::TestSupport {

/// Walks up from the current working directory looking for Resources/ShadersSlang,
/// which only exists at the repo root. Tests run with the repo root as the
/// working directory (gtest_discover_tests sets WORKING_DIRECTORY to
/// CMAKE_SOURCE_DIR), so this normally succeeds on the first iteration - the
/// depth-6 bound is tolerance for being invoked from somewhere under a build
/// directory instead, not a hard requirement. Returns a default-constructed
/// (empty) path if no such ancestor is found.
inline std::filesystem::path repoRoot()
{
    std::filesystem::path candidate = std::filesystem::current_path();
    for (int depth = 0; depth < 6; ++depth) {
        if (std::filesystem::exists(candidate / "Resources" / "ShadersSlang")) { return candidate; }
        if (!candidate.has_parent_path()) { break; }
        candidate = candidate.parent_path();
    }
    return {};
}

/// Resources/ShadersSlang under repoRoot() - every Slang shader source lives here.
inline std::filesystem::path slangRoot() { return repoRoot() / "Resources" / "ShadersSlang"; }

/// slangRoot()'s compiled-SPIR-V output directory.
inline std::filesystem::path spirvRoot() { return slangRoot() / "build" / "spirv"; }

/// Reads `path` in full, or std::nullopt if it cannot be opened or read.
inline std::optional<std::string> readFileText(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) { return std::nullopt; }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

}// namespace Kataglyphis::TestSupport
