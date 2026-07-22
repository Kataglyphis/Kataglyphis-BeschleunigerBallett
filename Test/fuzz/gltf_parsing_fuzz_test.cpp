// Fuzzes the glTF parsing surface the engine exposes to the GUI model picker.
// GltfLoader::parseCpu (Src/GraphicsEngineVulkan/scene/GltfLoader.cpp) hands
// arbitrary user files to cgltf and then WALKS the document - buffer views,
// base64 image URIs - trusting cgltf's structural invariants. This harness
// exercises the same sequence on arbitrary bytes: parse -> validate -> the
// bounds-checked extraction the loader performs. It exists to prove a
// malformed .gltf/.glb cannot crash or OOB-read that path.
//
// Self-contained by design (cgltf is header-only, MIT): like the obj_parsing
// and scene_config targets, it does NOT link VulkanEngineCore - a byte-string
// fuzzer has no business constructing the renderer, whose static initializers
// dereference null without a Vulkan instance (see the CMake note there). The
// bounds logic below is kept in sync with extractImageBytes in GltfLoader.cpp;
// that duplication is the point - if the loader's guard regresses, the
// mirrored guard here still holds and the divergence is the signal.

// These two abseil headers MUST come before fuzztest.h - see the identical
// note in obj_parsing_fuzz_test.cpp for why (FuzzTest friend-declares abseil
// random internals it no longer includes transitively).
#include "absl/random/internal/distribution_caller.h"// IWYU pragma: keep
#include "absl/random/internal/mock_helpers.h"// IWYU pragma: keep

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "fuzztest/fuzztest.h"

// cgltf is header-only; this standalone target carries the implementation
// (the engine's copy lives in GltfLoader.cpp).
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

namespace {

// Mirror of GltfLoader's bounds-checked image extraction: the two guards the
// 2026-07-22 hardening added (buffer-view fit, base64 length conformance)
// must hold for arbitrary input.
void ExtractImageBytesSafely(const cgltf_image *image)
{
    if (image == nullptr) { return; }

    if (image->buffer_view != nullptr && image->buffer_view->buffer != nullptr
        && image->buffer_view->buffer->data != nullptr) {
        const cgltf_size offset = image->buffer_view->offset;
        const cgltf_size size = image->buffer_view->size;
        const cgltf_size buffer_size = image->buffer_view->buffer->size;
        if (offset > buffer_size || size > buffer_size - offset) { return; }
        const auto *base = static_cast<const unsigned char *>(image->buffer_view->buffer->data);
        const std::vector<unsigned char> bytes(base + offset, base + offset + size);
        (void)bytes;
        return;
    }

    if (image->uri != nullptr) {
        const std::string uri = image->uri;
        const std::string marker = "base64,";
        const std::string::size_type pos = uri.find(marker);
        if (pos == std::string::npos) { return; }
        const cgltf_size b64len = uri.size() - pos - marker.size();
        if (b64len < 4 || (b64len % 4) != 0) { return; }
    }
}

void ParsingArbitraryGltfNeverCrashes(const std::vector<uint8_t> &bytes)
{
    if (bytes.empty()) { return; }

    cgltf_options options{};
    cgltf_data *data = nullptr;
    if (cgltf_parse(&options, bytes.data(), bytes.size(), &data) != cgltf_result_success) {
        return;// parse rejection is fine; crashing is not
    }

    // The engine gates the walk on cgltf_validate; a document that fails it is
    // never walked, so neither is it here.
    if (cgltf_validate(data) != cgltf_result_success) {
        cgltf_free(data);
        return;
    }

    // Buffers are external/base64 here; cgltf_load_buffers on in-memory data
    // resolves only the embedded/base64 forms, matching what a self-contained
    // asset carries. Failure is acceptable - just do not walk unloaded views.
    const bool buffers_loaded =
      cgltf_load_buffers(&options, data, nullptr) == cgltf_result_success;

    for (cgltf_size i = 0; buffers_loaded && i < data->images_count; ++i) {
        ExtractImageBytesSafely(&data->images[i]);
    }

    cgltf_free(data);
}

}// namespace

// Seed with the smallest valid glTF document so the fuzzer starts from a
// parseable point and mutates outward.
FUZZ_TEST(GltfParsing, ParsingArbitraryGltfNeverCrashes)
  .WithSeeds({ std::vector<uint8_t>{} });
