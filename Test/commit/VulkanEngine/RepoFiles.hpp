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
#include <system_error>
#include <vector>

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

/// True only for a path that names a regular file. Both readers gate on this
/// because open(2) on a directory *succeeds* on Linux - std::ifstream reports a
/// good stream and then fails on the first read, so a directory would otherwise
/// read as an empty file there while failing to open at all on Windows. Uses the
/// std::error_code overload: this project builds with exceptions disabled, so
/// the throwing form would terminate on a permission error instead of returning.
inline bool isReadableRegularFile(const std::filesystem::path &path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

/// Reads `path` in full, or std::nullopt if it is not a regular file or cannot
/// be opened or read.
inline std::optional<std::string> readFileText(const std::filesystem::path &path)
{
    if (!isReadableRegularFile(path)) { return std::nullopt; }
    std::ifstream file(path, std::ios::binary);
    if (!file) { return std::nullopt; }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

/// Reads `path` line by line, or std::nullopt if it is not a regular file or
/// cannot be opened. An
/// existing empty file returns an empty vector, distinct from std::nullopt -
/// callers rely on that distinction. Opens in binary mode so behavior is
/// identical on Linux and Windows, then strips one trailing '\r' from each
/// line: text-mode getline() strips '\r' for CRLF files on Windows but not on
/// Linux, and .gitattributes pins several of the files parsed by this suite
/// (*.ps1, *.psm1, *.cmd) to CRLF, so without the strip the parsed lines would
/// differ by platform.
inline std::optional<std::vector<std::string>> readFileLines(const std::filesystem::path &path)
{
    if (!isReadableRegularFile(path)) { return std::nullopt; }
    std::ifstream file(path, std::ios::binary);
    if (!file) { return std::nullopt; }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }
        lines.push_back(std::move(line));
    }
    return lines;
}

}// namespace Kataglyphis::TestSupport
