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
#include <vector>

import kataglyphis.vulkan.shader_helper;

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
