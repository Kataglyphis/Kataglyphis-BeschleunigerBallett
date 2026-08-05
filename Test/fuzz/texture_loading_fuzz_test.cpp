// Fuzzes texture decoding - the last untrusted input surface in the engine.
//
// Every material texture and every skybox face reaches stb_image through
// TextureDecode::decodeImageRGBA8 - the function Texture::loadTextureData
// forwards to - which returns a raw pointer plus dimensions and a
// byte count that callers then use to size a staging buffer and memcpy into
// it. A decoder that reports dimensions inconsistent with the buffer it
// returned is therefore a heap overflow one memcpy later, not a bad picture.
//
// (KTX2 is deliberately not covered: the C++ engine does not use it. The KTX
// dependency is consumed by the Rust renderer, which has its own tests.)
//
// The property under test is the CONTRACT, not merely "does not crash":
// either the load fails and returns null, or it succeeds and
// image_size == width * height * 4 with both dimensions positive. A decoder
// that returned a small buffer while reporting large dimensions would pass a
// crash-only test and corrupt the heap in production.

// These two abseil headers MUST come before fuzztest.h. FuzzTest at main
// friend-declares absl::random_internal::{DistributionCaller, MockHelpers}
// in fuzzing_bit_gen.h without including them, and abseil LTS 20260526 no
// longer provides them transitively through bit_gen_ref.h. The fuzztest_*
// library targets get the same fix as a force-include flag
// (ExternalLib/CMakeLists.txt); OUR targets cannot, because a force-include
// flag flows into the synthesized C++20 module BMI compiles of imported
// engine modules, which have no abseil include path. An ordinary include in
// the source is invisible to module synthesis.
#include "absl/random/internal/distribution_caller.h"// IWYU pragma: keep
#include "absl/random/internal/mock_helpers.h"// IWYU pragma: keep

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

// stb_image is header-only and this target does not link the engine, so it
// carries its own implementation - exactly as gltf_parsing_fuzz_test does with
// CGLTF_IMPLEMENTATION. The engine keeps its own copy in VulkanRenderer.cpp;
// the two never meet in one link.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "fuzztest/fuzztest.h"

import kataglyphis.vulkan.texture_decode;

namespace {

void DecodingArbitraryImageBytesRespectsTheSizeContract(const std::vector<uint8_t> &contents)
{
    if (contents.size() > 1u << 18) { return; }

    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    if (ec) { return; }
    const std::filesystem::path path = dir / "kataglyphis_texture_fuzz.img";

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) { return; }
        if (!contents.empty()) {
            out.write(reinterpret_cast<const char *>(contents.data()), static_cast<std::streamsize>(contents.size()));
        }
        if (!out) { return; }
    }

    // Deliberately pre-set to sentinels rather than 0. stb_image itself does
    // not write these on failure, but decodeImageRGBA8 zeros
    // width/height/image_size before returning null, overwriting whatever
    // sentinel was here; the assertions below only look at them on the
    // success path, which is the contract callers are entitled to rely on.
    int width = -1;
    int height = -1;
    std::uint64_t image_size = 0;
    unsigned char *pixels = Kataglyphis::TextureDecode::decodeImageRGBA8(path.string(), &width, &height, &image_size);

    if (pixels != nullptr) {
        EXPECT_GT(width, 0) << "decode succeeded but reported a non-positive width";
        EXPECT_GT(height, 0) << "decode succeeded but reported a non-positive height";
        // The exact relationship callers depend on when sizing a staging
        // buffer: STBI_rgb_alpha forces 4 channels.
        EXPECT_EQ(image_size,
          static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4U)
          << "reported byte count does not match the reported dimensions - a staging "
             "buffer sized from this would over- or under-run";
        stbi_image_free(pixels);
    }

    std::filesystem::remove(path, ec);
}

FUZZ_TEST(TextureLoadingFuzz, DecodingArbitraryImageBytesRespectsTheSizeContract)
  .WithSeeds({ std::vector<uint8_t>{},
    // PNG magic - enough to enter the PNG decoder and then fail.
    std::vector<uint8_t>{ 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a },
    // JPEG SOI.
    std::vector<uint8_t>{ 0xff, 0xd8, 0xff, 0xe0 },
    // BMP.
    std::vector<uint8_t>{ 'B', 'M' },
    // TGA-ish header with absurd dimensions, which is how a decoder is
    // talked into reporting a size it did not allocate.
    std::vector<uint8_t>{ 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 32, 0 },
    std::vector<uint8_t>{ 'G', 'I', 'F', '8', '9', 'a' } });

}// namespace
