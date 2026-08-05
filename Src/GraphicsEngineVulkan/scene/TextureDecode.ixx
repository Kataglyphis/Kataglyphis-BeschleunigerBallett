module;

#include <cstdint>
#include <string>

#include <stb_image.h>

// The image decode every material texture and every skybox face goes through,
// as its own module so it can be exercised without the renderer.
//
// It used to live in Texture.cpp, which means the only way to reach it was to
// link VulkanEngineCore. texture_loading_fuzz_test did exactly that and died
// at startup on the Linux ASan lane - "SEGV on unknown address
// 0x000000000000" inside abseil's flag registry, from fuzztest's own static
// initializer (init_fuzztest.cc:54), before a single test ran. Adding the
// engine's translation units to that link reorders the static initializers
// FuzzTest's abseil flags are registered by; the other fuzz targets stay
// minimal for this reason and Test/fuzz/CMakeLists.txt documents the same
// crash for scene_config_fuzz_test.
//
// Nothing here needs Vulkan, a device, or a logger: it is stb_image plus the
// size arithmetic callers rely on. Kataglyphis::Texture::loadTextureData
// keeps its signature and its spdlog error line, and forwards here.
export module kataglyphis.vulkan.texture_decode;

export namespace Kataglyphis::TextureDecode {

// Decodes an image file to tightly packed RGBA8. Returns nullptr on failure;
// on success `*image_size` is exactly width * height * 4 and the caller owns
// the buffer (free with stbi_image_free).
//
// The size contract is the point, not merely "does not crash". The byte count
// reported here sizes a staging buffer that is then memcpy'd into, so a count
// that disagrees with the returned allocation is a heap overflow rather than a
// bad picture. STBI_rgb_alpha forces 4 channels, which is what makes the
// width * height * 4 arithmetic exact.
//
// On failure the three out-parameters are ZEROED rather than left alone.
// stb_image does not write them on the failure path, so a caller that checked
// the size before the null pointer would otherwise read whatever it had
// initialised them to.
inline unsigned char *decodeImageRGBA8(const std::string &file_name, int *width, int *height, std::uint64_t *image_size)
{
    int channels = 0;
    unsigned char *image = stbi_load(file_name.c_str(), width, height, &channels, STBI_rgb_alpha);

    if (image == nullptr) {
        *width = 0;
        *height = 0;
        *image_size = 0;
        return nullptr;
    }

    *image_size = static_cast<std::uint64_t>(*width) * static_cast<std::uint64_t>(*height) * 4U;

    return image;
}

}// namespace Kataglyphis::TextureDecode
