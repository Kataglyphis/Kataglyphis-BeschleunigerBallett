// Fuzzes the OBJ parsing path the engine depends on: tinyobj parsing of
// arbitrary input plus the exact index-walking pattern used by
// ObjLoader::loadVertices (Src/GraphicsEngineVulkan/scene/ObjLoader.cpp).
// Keep the walk in sync with the engine - this harness exists to prove
// malformed model files cannot crash or OOB-read the loader.

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

#include <cstddef>
#include <string>
#include <vector>

#include "fuzztest/fuzztest.h"

// tinyobjloader is header-only; this standalone target must carry the
// implementation itself (the engine's copy lives in ObjLoader.cpp).
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

namespace {

void ParsingArbitraryObjNeverCrashes(const std::string &obj_text, const std::string &mtl_text)
{
    tinyobj::ObjReader reader;
    tinyobj::ObjReaderConfig config;
    config.triangulate = true;

    if (!reader.ParseFromString(obj_text, mtl_text, config)) {
        return;// parse rejection is fine; crashing is not
    }

    const auto &attrib = reader.GetAttrib();

    // Mirror of ObjLoader::loadVertices' guarded access pattern.
    float checksum = 0.0F;
    for (const auto &shape : reader.GetShapes()) {
        size_t index_offset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            const auto fv = static_cast<size_t>(shape.mesh.num_face_vertices[f]);
            for (size_t v = 0; v < fv; v++) {
                if (index_offset + v >= shape.mesh.indices.size()) { break; }
                tinyobj::index_t const idx = shape.mesh.indices[index_offset + v];

                const auto vertex_index = static_cast<size_t>(idx.vertex_index);
                if (idx.vertex_index < 0 || (3 * vertex_index) + 2 >= attrib.vertices.size()) {
                    continue;
                }
                checksum += attrib.vertices[(3 * vertex_index) + 0];

                if (idx.normal_index >= 0
                    && (3 * static_cast<size_t>(idx.normal_index)) + 2 < attrib.normals.size()) {
                    checksum += attrib.normals[3 * static_cast<size_t>(idx.normal_index)];
                }
                if ((3 * vertex_index) + 2 < attrib.colors.size()) {
                    checksum += attrib.colors[3 * vertex_index];
                }
                if (idx.texcoord_index >= 0
                    && (2 * static_cast<size_t>(idx.texcoord_index)) + 1 < attrib.texcoords.size()) {
                    checksum += attrib.texcoords[2 * static_cast<size_t>(idx.texcoord_index)];
                }
            }
            index_offset += fv;
        }
    }
    (void)checksum;
}

FUZZ_TEST(ObjParsingFuzz, ParsingArbitraryObjNeverCrashes)
  .WithSeeds({ { "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", "" },
    { "v 0 0 0\nf -1 -2 -3\n", "" },
    { "v 1 2 3\nvn 0 1 0\nvt 0.5 0.5\nf 1/1/1 1/1/1 1/1/1\n", "newmtl m\nKd 1 0 0\n" } });

}// namespace
