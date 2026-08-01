// Direct unit coverage for kataglyphis.vulkan.shader_helper::validateSpirvBlob
// - the pure, CPU-testable check that loadSpirvShaderModule runs before
// handing bytes to vkCreateShaderModule. A missing or truncated .spv used to
// go straight through as an empty vector (codeSize = 0), which is a
// validation error at best in debug and undefined driver behaviour in
// release. This pins the structural check without touching Vulkan.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <type_traits>
#include <vector>

import kataglyphis.vulkan.shader_helper;

using Kataglyphis::ShaderStagePair;
using Kataglyphis::validateSpirvBlob;

namespace {

// SPIR-V magic number, little endian - a real shader's first four bytes.
std::vector<char> spirvMagicBytes()
{
    return { static_cast<char>(0x03), static_cast<char>(0x02), static_cast<char>(0x23), static_cast<char>(0x07) };
}

}// namespace

TEST(ShaderBlobUnit, EmptyBlobIsRejected)
{
    EXPECT_FALSE(validateSpirvBlob(std::span<const char>{}));
}

TEST(ShaderBlobUnit, SizeNotAMultipleOfFourIsRejected)
{
    // Six bytes: not a multiple of 4, even though the first four are the
    // correct magic number.
    std::vector<char> blob = spirvMagicBytes();
    blob.push_back(0);
    blob.push_back(0);
    EXPECT_FALSE(validateSpirvBlob(blob));
}

TEST(ShaderBlobUnit, CorrectSizeWithWrongMagicIsRejected)
{
    const std::vector<char> blob(8, 0);// 8 bytes, all zero - wrong magic
    EXPECT_FALSE(validateSpirvBlob(blob));
}

TEST(ShaderBlobUnit, BlobWithCorrectMagicIsAccepted)
{
    const std::vector<char> blob = spirvMagicBytes();
    EXPECT_TRUE(validateSpirvBlob(blob));
}

// A real compiled shader, read exactly as buildIntegritySuite.cpp does (tests
// run with the repo root as the working directory).
TEST(ShaderBlobUnit, RealCompiledShaderIsAccepted)
{
    const std::filesystem::path spv = "Resources/ShadersSlang/build/spirv/rasterizer/rasterizer.vs_main.spv";
    if (!std::filesystem::exists(spv)) {
        GTEST_SKIP() << spv.string() << " not found - run compile-slang-shaders before this test.";
    }

    std::ifstream file(spv, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(file.is_open());
    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0);
    std::vector<char> bytes(size);
    file.read(bytes.data(), static_cast<std::streamsize>(size));

    EXPECT_TRUE(validateSpirvBlob(bytes));
}

// ShaderStagePair owns two vk::ShaderModule handles released in its
// destructor - copying would let two instances both destroy the same
// modules (double-destroy), and moving would leave the create-infos in the
// moved-from instance pointing at handles the moved-to instance now owns.
// Every call site constructs it as a function-local, so deleting both
// outright (rather than making it move-only) costs nothing there and rules
// out that whole class of bug. Needs no device to check, so it runs in
// Windows CI.
TEST(ShaderBlobUnit, ShaderStagePairIsNeitherCopyableNorMovable)
{
    static_assert(!std::is_copy_constructible_v<ShaderStagePair>, "ShaderStagePair must not be copy-constructible");
    static_assert(!std::is_copy_assignable_v<ShaderStagePair>, "ShaderStagePair must not be copy-assignable");
    static_assert(!std::is_move_constructible_v<ShaderStagePair>, "ShaderStagePair must not be move-constructible");
    static_assert(!std::is_move_assignable_v<ShaderStagePair>, "ShaderStagePair must not be move-assignable");
}
