// Fuzzes the file reader every shader load goes through.
//
// ShaderHelper builds a path, wraps it in Kataglyphis::File, and calls
// readCharSequence(); the bytes become a VkShaderModule with no further
// validation. Two things therefore matter, and only one of them is about
// crashing:
//
//   - a hostile or malformed PATH must not crash or hang;
//   - the CONTENT must come back byte-exact. SPIR-V is binary: it contains
//     embedded NULs and arbitrary byte values, so a reader that opens in text
//     mode, stops at a NUL, or rewrites CRLF would silently hand a truncated
//     or mangled module to the driver. That corrupts shaders without ever
//     raising an error, which is the failure this repo has already paid for
//     once via stale SPIR-V.
//
// The round-trip property is the point. A "does not crash" fuzz test over a
// reader that quietly truncates would pass forever.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fuzztest/fuzztest.h"

import kataglyphis.vulkan.file;

namespace {

// Reading a path that may be malformed, absent, hostile or simply strange
// must return empty rather than crash. File::read/readCharSequence guard with
// fileExists, so the interesting inputs are the ones that make path
// construction itself misbehave.
void ReadingArbitraryPathsNeverCrashes(const std::string &path)
{
    // Beyond this only the OS path limit is under test, not our logic.
    if (path.size() > 512) { return; }

    Kataglyphis::File file(path);
    const std::vector<char> bytes = file.readCharSequence();
    const std::string text = file.read();
    const std::string base = file.getBaseDir();

    (void)bytes.size();
    (void)text.size();
    (void)base.size();
}

FUZZ_TEST(ShaderFileReaderFuzz, ReadingArbitraryPathsNeverCrashes)
  .WithSeeds({ std::string(""),
    std::string("Resources/Shaders/rasterizer/spv/shader.frag.spv"),
    std::string("does/not/exist.spv"),
    std::string("../../../etc/passwd"),
    std::string("C:\\Windows\\System32\\config\\SAM"),
    std::string("\xff\xfe\x00 binary in a path"),
    std::string("con"),// reserved device name on Windows
    std::string(".") });

// The property that actually protects shader loading: whatever bytes are on
// disk come back identical. Embedded NULs, 0x1a (historic EOF), and CR/LF
// pairs are all present in real SPIR-V and are exactly what a text-mode read
// would damage.
void ReadingAFileReturnsItsBytesExactly(const std::vector<uint8_t> &contents)
{
    if (contents.size() > 1u << 16) { return; }

    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    if (ec) { return; }
    const std::filesystem::path path = dir / "kataglyphis_shader_reader_fuzz.bin";

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) { return; }
        if (!contents.empty()) {
            out.write(reinterpret_cast<const char *>(contents.data()), static_cast<std::streamsize>(contents.size()));
        }
        if (!out) { return; }
    }

    Kataglyphis::File file(path.string());
    const std::vector<char> read_back = file.readCharSequence();

    // Byte count first: a truncation at the first NUL shows up here, and the
    // message is far clearer than a mismatch deep in the comparison.
    ASSERT_EQ(read_back.size(), contents.size())
      << "readCharSequence returned " << read_back.size() << " of " << contents.size()
      << " bytes - a shader read this way would be silently truncated";

    for (std::size_t i = 0; i < contents.size(); ++i) {
        ASSERT_EQ(static_cast<uint8_t>(read_back[i]), contents[i])
          << "byte " << i << " changed on read - binary content is being transformed";
    }

    std::filesystem::remove(path, ec);
}

FUZZ_TEST(ShaderFileReaderFuzz, ReadingAFileReturnsItsBytesExactly)
  .WithSeeds({ std::vector<uint8_t>{},
    // SPIR-V magic number, little endian - a real shader's first four bytes.
    std::vector<uint8_t>{ 0x03, 0x02, 0x23, 0x07 },
    std::vector<uint8_t>{ 0x00, 0x00, 0x00, 0x00 },
    std::vector<uint8_t>{ 'a', 0x00, 'b' },
    std::vector<uint8_t>{ 0x0d, 0x0a, 0x0d, 0x0a },// CRLF, mangled by text mode
    std::vector<uint8_t>{ 0x1a },// historic DOS EOF
    std::vector<uint8_t>{ 0xff, 0xfe, 0xef, 0xbb, 0xbf } });

}// namespace
