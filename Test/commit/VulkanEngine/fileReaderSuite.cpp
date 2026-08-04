// Deterministic counterpart to Test/fuzz/shader_file_reader_fuzz_test.cpp and
// texture_loading_fuzz_test.cpp. Fuzzing proves "no crash on arbitrary
// bytes"; it does not pin what a function returns for a specific known-bad
// input, and it does not run on the Windows CPU lane. These tests pin the
// exact contract callers rely on.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <vulkan/vulkan.hpp>

import kataglyphis.shared.util.file_reader;
import kataglyphis.shared.util.resource_paths;
import kataglyphis.vulkan.texture;

using Kataglyphis::Shared::fileExists;
using Kataglyphis::Shared::getBaseDir;
using Kataglyphis::Shared::readBinaryFile;
using Kataglyphis::Shared::readTextFile;
using Kataglyphis::Shared::resolveResourceRelativePath;

namespace {

std::filesystem::path uniqueTempPath(const std::string &name)
{
    return std::filesystem::temp_directory_path() / name;
}

// Swaps the process working directory for the lifetime of the guard and puts
// it back on destruction - resolveResourceRelativePath reads
// std::filesystem::current_path() internally, and the working directory is
// process-global state shared with every other test in this binary.
class ScopedWorkingDirectory
{
  public:
    explicit ScopedWorkingDirectory(const std::filesystem::path &newCwd)
    {
        std::error_code ec;
        original_ = std::filesystem::current_path(ec);
        std::filesystem::current_path(newCwd, ec);
    }
    ~ScopedWorkingDirectory()
    {
        std::error_code ec;
        std::filesystem::current_path(original_, ec);
    }
    ScopedWorkingDirectory(const ScopedWorkingDirectory &) = delete;
    ScopedWorkingDirectory &operator=(const ScopedWorkingDirectory &) = delete;

  private:
    std::filesystem::path original_;
};

}// namespace

TEST(FileReaderUnit, FileExistsFalseForMissingPath)
{
    const auto missing = uniqueTempPath("kat_filereader_does_not_exist.txt");
    std::error_code ec;
    std::filesystem::remove(missing, ec);

    EXPECT_FALSE(fileExists(missing.string()));
}

TEST(FileReaderUnit, FileExistsFalseNotCrashForUnstattablePath)
{
    // A single path component longer than the filesystem's name limit (255
    // on both NTFS and ext4) makes the OS query itself fail
    // (ENAMETOOLONG / ERROR_FILENAME_EXCED_RANGE) rather than cleanly report
    // "not found". The throwing std::filesystem::exists() overload reports
    // that as an exception; with exceptions disabled project-wide that would
    // be a terminate. fileExists must use the error_code overload and simply
    // report false.
    const std::string overlong_component(300, 'a');
    const std::string unstattable = (std::filesystem::temp_directory_path() / overlong_component).string();

    EXPECT_FALSE(fileExists(unstattable));
}

TEST(FileReaderUnit, ReadTextFileEmptyForMissingPath)
{
    const auto missing = uniqueTempPath("kat_filereader_missing.txt");
    std::error_code ec;
    std::filesystem::remove(missing, ec);

    EXPECT_TRUE(readTextFile(missing.string()).empty());
}

TEST(FileReaderUnit, ReadTextFileEmptyForDirectoryPath)
{
    const auto dir = uniqueTempPath("kat_filereader_text_dir");
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);

    EXPECT_TRUE(readTextFile(dir.string()).empty());

    std::filesystem::remove(dir, ec);
}

TEST(FileReaderUnit, ReadBinaryFileEmptyForMissingPath)
{
    const auto missing = uniqueTempPath("kat_filereader_missing.bin");
    std::error_code ec;
    std::filesystem::remove(missing, ec);

    EXPECT_TRUE(readBinaryFile(missing.string()).empty());
}

TEST(FileReaderUnit, ReadBinaryFileEmptyForDirectoryPath)
{
    const auto dir = uniqueTempPath("kat_filereader_binary_dir");
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);

    EXPECT_TRUE(readBinaryFile(dir.string()).empty());

    std::filesystem::remove(dir, ec);
}

TEST(FileReaderUnit, ReadBinaryFileRoundTripsEmbeddedNulAndCrlfBytes)
{
    const auto path = uniqueTempPath("kat_filereader_roundtrip.bin");
    const std::vector<char> contents = { 'A', '\0', '\r', '\n', 'B', '\r', '\n', '\0' };
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    const std::vector<char> read_back = readBinaryFile(path.string());
    EXPECT_EQ(read_back, contents) << "binary read must not do text translation "
                                       "(CRLF collapsing / NUL truncation)";

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(FileReaderUnit, ReadBinaryFileEmptyFileReturnsEmptyVectorNotNullRead)
{
    const auto path = uniqueTempPath("kat_filereader_empty.bin");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
    }

    EXPECT_TRUE(readBinaryFile(path.string()).empty());

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(FileReaderUnit, ReadTextFileAppendsTrailingNewlineWhenSourceHasNone)
{
    const auto path = uniqueTempPath("kat_filereader_no_trailing_newline.txt");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "abc";// deliberately no trailing '\n'
    }

    EXPECT_EQ(readTextFile(path.string()), "abc\n");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(FileReaderUnit, ReadTextFilePreservesEmbeddedNulByte)
{
    // Mirrors the fuzz round-trip property (ReadingAFileReturnsItsBytesExactly
    // in shader_file_reader_fuzz_test.cpp), pinned as a deterministic case:
    // std::getline reads up to '\n', not up to a NUL, so an embedded NUL must
    // survive inside the returned std::string rather than truncating it.
    const auto path = uniqueTempPath("kat_filereader_embedded_nul.bin");
    const std::vector<char> contents = { 'A', '\0', 'B', '\n' };
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    const std::string text = readTextFile(path.string());
    const std::string expected(contents.data(), contents.size());
    EXPECT_EQ(text, expected) << "embedded NUL byte was dropped or truncated by a text-mode read";

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(FileReaderUnit, GetBaseDirSlashOnly)
{
    EXPECT_EQ(getBaseDir("a/b/c.txt"), "a/b");
}

TEST(FileReaderUnit, GetBaseDirBackslashOnly)
{
    EXPECT_EQ(getBaseDir("a\\b\\c.txt"), "a\\b");
}

TEST(FileReaderUnit, GetBaseDirMixedSeparators)
{
    EXPECT_EQ(getBaseDir("a/b\\c.txt"), "a/b");
}

TEST(FileReaderUnit, GetBaseDirNoSeparatorReturnsEmpty)
{
    EXPECT_TRUE(getBaseDir("c.txt").empty());
}

// Texture::loadTextureData is static and touches no device, so it stays
// CPU-only here rather than in an integration/golden suite.
TEST(TextureLoadUnit, FailedDecodeZeroesAllOutputs)
{
    const auto path = uniqueTempPath("kat_texture_not_an_image.bin");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const std::vector<uint8_t> garbage = { 0x00, 0x01, 0x02, 0x03, 0x04 };
        out.write(reinterpret_cast<const char *>(garbage.data()), static_cast<std::streamsize>(garbage.size()));
    }

    // Sentinels, not zero-initialized - a caller that trusted the old
    // contract (image_size computed from whatever width/height happened to
    // hold) would size a staging buffer from garbage.
    int width = -1;
    int height = -1;
    vk::DeviceSize image_size = 4;
    unsigned char *pixels = Kataglyphis::Texture::loadTextureData(path.string(), &width, &height, &image_size);

    EXPECT_EQ(pixels, nullptr);
    EXPECT_EQ(width, 0);
    EXPECT_EQ(height, 0);
    EXPECT_EQ(image_size, 0U);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// resolveResourceRelativePath consolidates what used to be four independent
// "walk up looking for Resources/" copies (SceneConfig::resolveModelPath,
// SceneConfig::findResourcesBasePath, the ImGui font resolver and the
// skybox loader). These cases pin the walk itself against a scratch tree,
// independent of the real repo layout.
TEST(ResourcePathsUnit, ResolvesAtDepthZero)
{
    const auto root = uniqueTempPath("kat_respath_depth0");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "Resources" / "Models", ec);
    ASSERT_FALSE(ec);

    ScopedWorkingDirectory cwdGuard(root);
    const auto resolved = resolveResourceRelativePath("Models");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_TRUE(std::filesystem::equivalent(*resolved, root / "Resources" / "Models"));

    std::filesystem::remove_all(root, ec);
}

TEST(ResourcePathsUnit, ResolvesAtDepthThree)
{
    const auto root = uniqueTempPath("kat_respath_depth3");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto leaf = root / "a" / "b" / "c";
    std::filesystem::create_directories(leaf, ec);
    std::filesystem::create_directories(root / "Resources" / "Models", ec);
    ASSERT_FALSE(ec);

    ScopedWorkingDirectory cwdGuard(leaf);
    const auto resolved = resolveResourceRelativePath("Models");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_TRUE(std::filesystem::equivalent(*resolved, root / "Resources" / "Models"));

    std::filesystem::remove_all(root, ec);
}

TEST(ResourcePathsUnit, MissesPastTheDepthCap)
{
    const auto root = uniqueTempPath("kat_respath_toodeep");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    // kResourceSearchDepth == 8 tests cwd plus 7 parents; put the leaf 8
    // levels below root so "Resources/Models" (at root) is one hop past
    // what the walk ever reaches.
    auto leaf = root;
    for (int i = 0; i < 8; ++i) { leaf /= "d" + std::to_string(i); }
    std::filesystem::create_directories(leaf, ec);
    std::filesystem::create_directories(root / "Resources" / "Models", ec);
    ASSERT_FALSE(ec);

    ScopedWorkingDirectory cwdGuard(leaf);
    EXPECT_FALSE(resolveResourceRelativePath("Models").has_value());

    std::filesystem::remove_all(root, ec);
}

TEST(ResourcePathsUnit, RejectsRelativePathEscapingTheTree)
{
    const auto root = uniqueTempPath("kat_respath_escape");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    ASSERT_FALSE(ec);

    ScopedWorkingDirectory cwdGuard(root);
    EXPECT_FALSE(resolveResourceRelativePath("../etc/passwd").has_value());

    std::filesystem::remove_all(root, ec);
}
