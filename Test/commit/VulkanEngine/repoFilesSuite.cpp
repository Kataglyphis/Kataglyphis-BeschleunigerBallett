// Pins the contract of RepoFiles.hpp's readFileLines(), which
// buildIntegritySuite.cpp now relies on for every line-based source scan.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "RepoFiles.hpp"

using Kataglyphis::TestSupport::readFileLines;

namespace {

std::filesystem::path uniqueTempPath(const std::string &name) { return std::filesystem::temp_directory_path() / name; }

void writeFile(const std::filesystem::path &path, const std::string &contents)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

}// namespace

TEST(RepoFilesUnit, ReadFileLinesNulloptForMissingPath)
{
    const auto missing = uniqueTempPath("kat_repofiles_missing.txt");
    std::error_code ec;
    std::filesystem::remove(missing, ec);

    EXPECT_FALSE(readFileLines(missing).has_value());
}

TEST(RepoFilesUnit, ReadFileLinesNulloptForDirectoryPath)
{
    const auto dir = uniqueTempPath("kat_repofiles_dir");
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);

    EXPECT_FALSE(readFileLines(dir).has_value());

    std::filesystem::remove(dir, ec);
}

TEST(RepoFilesUnit, ReadFileLinesEmptyFileReturnsEmptyVectorNotNullopt)
{
    const auto path = uniqueTempPath("kat_repofiles_empty.txt");
    writeFile(path, "");

    const auto lines = readFileLines(path);
    ASSERT_TRUE(lines.has_value());
    EXPECT_TRUE(lines->empty());

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(RepoFilesUnit, ReadFileLinesSplitsOnLf)
{
    const auto path = uniqueTempPath("kat_repofiles_lf.txt");
    writeFile(path, "a\nb\n");

    const auto lines = readFileLines(path);
    ASSERT_TRUE(lines.has_value());
    EXPECT_EQ(*lines, (std::vector<std::string>{ "a", "b" }));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(RepoFilesUnit, ReadFileLinesStripsCrlf)
{
    const auto path = uniqueTempPath("kat_repofiles_crlf.txt");
    writeFile(path, "a\r\nb\r\n");

    const auto lines = readFileLines(path);
    ASSERT_TRUE(lines.has_value());
    EXPECT_EQ(*lines, (std::vector<std::string>{ "a", "b" }))
      << "CRLF line endings must produce the same lines on Linux and Windows";

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(RepoFilesUnit, ReadFileLinesReturnsLastLineWithNoTrailingNewline)
{
    const auto path = uniqueTempPath("kat_repofiles_no_trailing_newline.txt");
    writeFile(path, "a\nb");

    const auto lines = readFileLines(path);
    ASSERT_TRUE(lines.has_value());
    EXPECT_EQ(*lines, (std::vector<std::string>{ "a", "b" }));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
