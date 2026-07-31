// Benchmarks for engine code that runs on the CPU every frame or on every
// asset load. Deliberately GPU-free: this suite must run in CI containers
// without an adapter, so nothing here touches Vulkan.
//
// Run (RelWithDebInfo only - debug timings are noise):
//   Build-Windows.ps1 -Configurations 'clangcl-profile'
//   build-clangcl-profile\perfTestSuite.exe
//   ... --benchmark_out=perf.json --benchmark_out_format=json  (for diffing)
//
// GPU-side per-pass timings are a separate mechanism: timestamp queries
// collected at runtime and shown in the GUI "GPU timings" panel.

#include <benchmark/benchmark.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Both parsers are compiled straight into this TU (their own implementation
// macros), exactly as the engine does in ObjLoader.cpp / cgltf_impl.cpp - NOT
// pulled from VulkanEngineCore. That is deliberate: driving the engine loaders
// would pull Device/Model/Texture (and their Vulkan-touching global ctors) into
// this headless benchmark binary. The benchmarks below want the parse cost, not
// the renderer, so they call the libraries directly like the OBJ path always has.
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

import kataglyphis.vulkan.camera;
import kataglyphis.vulkan.scene_config;
import kataglyphis.vulkan.cascaded_shadow_map;

namespace {

// ---------------------------------------------------------------- camera --
// Runs once per frame from the input handler; regressions show up as input
// latency rather than GPU time.

void BM_CameraKeyControl(benchmark::State &state)
{
    Camera camera;
    std::array<bool, 1024> keys{};
    keys[static_cast<size_t>('W')] = true;

    for (auto _ : state) {
        camera.key_control(keys.data(), 0.016F);
        benchmark::DoNotOptimize(camera.get_camera_position());
    }
}
BENCHMARK(BM_CameraKeyControl);

void BM_CameraMouseControl(benchmark::State &state)
{
    Camera camera;
    float delta = 1.0F;

    for (auto _ : state) {
        camera.mouse_control(delta, -delta);
        delta = -delta;// stay near the starting orientation
        benchmark::DoNotOptimize(camera.get_camera_direction());
    }
}
BENCHMARK(BM_CameraMouseControl);

void BM_CameraViewMatrix(benchmark::State &state)
{
    Camera camera;
    for (auto _ : state) { benchmark::DoNotOptimize(camera.calculate_viewmatrix()); }
}
BENCHMARK(BM_CameraViewMatrix);

// ------------------------------------------------------------ projection --
// Rebuilt every frame. The cloud shader now consumes CPU-side inverses, so
// that cost moved here on purpose - this benchmark is what guards it.

void BM_ProjectionAndInverses(benchmark::State &state)
{
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0F, 2.0F, 5.0F), glm::vec3(0.0F), glm::vec3(0.0F, 1.0F, 0.0F));
    for (auto _ : state) {
        glm::mat4 projection = glm::perspective(glm::radians(60.0F), 16.0F / 9.0F, 0.1F, 1000.0F);
        projection[1][1] *= -1.0F;
        benchmark::DoNotOptimize(projection);
        benchmark::DoNotOptimize(glm::inverse(projection));
        benchmark::DoNotOptimize(glm::inverse(view));
    }
}
BENCHMARK(BM_ProjectionAndInverses);

// ------------------------------------------------------- cascaded shadows --
// computeCascadeData is per-frame CPU math (VulkanRenderer::updateCascades ->
// CascadedShadowMap::updateCascades), not GPU work, and had zero perf
// coverage despite 19 correctness tests; the 2026-07-31 texel-snapping /
// clamping work touched exactly this function with no way to see a
// regression. Arguments mirror cascadedShadowMapSuite.cpp's default_view() /
// default_light(), and GUISceneSharedVars' default shadow_distance (60) and
// shadow map resolution (2048) - the stabilized (texel-snapped) path.

void BM_ComputeCascadeData(benchmark::State &state)
{
    const auto numCascades = static_cast<uint32_t>(state.range(0));
    const glm::mat4 view =
      glm::lookAt(glm::vec3(0.0F, 6.0F, 26.0F), glm::vec3(0.0F, 1.0F, 0.0F), glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::vec3 lightDir(-0.55F, -1.0F, -0.35F);
    constexpr float kFov = 45.0F;
    constexpr float kAspect = 16.0F / 9.0F;
    constexpr float kNear = 0.1F;
    constexpr float kFar = 150.0F;
    constexpr float kShadowDistance = 60.0F;
    constexpr uint32_t kShadowMapResolution = 2048;

    for (auto _ : state) {
        benchmark::DoNotOptimize(Kataglyphis::computeCascadeData(
          numCascades, view, kFov, kAspect, kNear, kFar, lightDir, kShadowDistance, 0.5F, kShadowMapResolution));
    }
}
BENCHMARK(BM_ComputeCascadeData)->Arg(1)->Arg(3);

// ----------------------------------------------------------- scene config --

// Paths are relative to Resources/ (see SceneConfig.cpp). The hit path
// short-circuits on the first exists(); the miss path walks up to 8 parent
// directories probing the filesystem, which is ~500x slower - worth knowing
// before calling this per frame or per model in a loop.
void BM_ResolveModelPath_Hit(benchmark::State &state)
{
    for (auto _ : state) { benchmark::DoNotOptimize(sceneConfig::resolveModelPath("Models/plane.obj")); }
}
BENCHMARK(BM_ResolveModelPath_Hit);

void BM_ResolveModelPath_Miss(benchmark::State &state)
{
    for (auto _ : state) { benchmark::DoNotOptimize(sceneConfig::resolveModelPath("Models/does_not_exist.obj")); }
}
BENCHMARK(BM_ResolveModelPath_Miss);

void BM_AvailableModelPaths(benchmark::State &state)
{
    for (auto _ : state) { benchmark::DoNotOptimize(sceneConfig::getAvailableModelPaths()); }
}
BENCHMARK(BM_AvailableModelPaths);

// -------------------------------------------------------------- obj load --
// Mirrors ObjLoader::loadVertices: parse, then walk faces with the same
// bounds-guarded accessor pattern. This dominates model load time and is
// GPU-free, so it belongs here rather than in a GPU test.

std::string find_model(const char *name)
{
    namespace fs = std::filesystem;
    for (const auto *prefix : { "", "../", "../../", "../../../" }) {
        fs::path candidate = fs::path(prefix) / "Resources" / "Models" / name;
        if (fs::exists(candidate)) { return candidate.string(); }
    }
    return {};
}

void parse_and_walk(const std::string &path, benchmark::State &state)
{
    if (path.empty()) {
        state.SkipWithError("model not found (run from the repo root)");
        return;
    }

    for (auto _ : state) {
        tinyobj::ObjReader reader;
        tinyobj::ObjReaderConfig config;
        config.triangulate = true;
        if (!reader.ParseFromFile(path, config)) {
            state.SkipWithError("failed to parse model");
            return;
        }

        const auto &attrib = reader.GetAttrib();
        double checksum = 0.0;
        for (const auto &shape : reader.GetShapes()) {
            size_t index_offset = 0;
            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
                const auto fv = static_cast<size_t>(shape.mesh.num_face_vertices[f]);
                for (size_t v = 0; v < fv; v++) {
                    if (index_offset + v >= shape.mesh.indices.size()) { break; }
                    const tinyobj::index_t idx = shape.mesh.indices[index_offset + v];
                    const auto vertex_index = static_cast<size_t>(idx.vertex_index);
                    if (idx.vertex_index < 0 || (3 * vertex_index) + 2 >= attrib.vertices.size()) { continue; }
                    checksum += attrib.vertices[3 * vertex_index];
                }
                index_offset += fv;
            }
        }
        benchmark::DoNotOptimize(checksum);
    }
}

void BM_ObjParse_Plane(benchmark::State &state) { parse_and_walk(find_model("plane.obj"), state); }
BENCHMARK(BM_ObjParse_Plane)->Unit(benchmark::kMicrosecond);

void BM_ObjParse_Suzanne(benchmark::State &state) { parse_and_walk(find_model("suzanne.obj"), state); }
BENCHMARK(BM_ObjParse_Suzanne)->Unit(benchmark::kMillisecond);

// ------------------------------------------------------------- gltf load --
// The glTF analogue of the OBJ benchmark: parse the document, load its buffers,
// then walk every primitive's POSITION accessor - the same parse-then-walk shape
// as parse_and_walk, over cgltf (the library GltfLoader::parseCpu is built on)
// rather than the engine loader, for the linkage reason noted at the includes.
//
// Two assets on purpose: the .glb carries its buffer inline as binary, the
// .gltf as a base64 data-URI, so cgltf_load_buffers exercises a different decode
// path for each. Both are small - these guard against a gross regression (a
// per-call allocation storm, an accidental O(n^2)), not scale, exactly like
// BM_ObjParse_Plane.

void parse_and_walk_gltf(const std::string &path, benchmark::State &state)
{
    if (path.empty()) {
        state.SkipWithError("gltf model not found (run from the repo root)");
        return;
    }

    for (auto _ : state) {
        cgltf_options options{};
        cgltf_data *data = nullptr;
        if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
            state.SkipWithError("failed to parse gltf model");
            return;
        }
        if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
            cgltf_free(data);
            state.SkipWithError("failed to load gltf buffers");
            return;
        }

        double checksum = 0.0;
        for (cgltf_size m = 0; m < data->meshes_count; ++m) {
            const cgltf_mesh &mesh = data->meshes[m];
            for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
                const cgltf_primitive &primitive = mesh.primitives[p];
                for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
                    const cgltf_attribute &attribute = primitive.attributes[a];
                    if (attribute.type != cgltf_attribute_type_position) { continue; }
                    const cgltf_accessor *accessor = attribute.data;
                    for (cgltf_size i = 0; i < accessor->count; ++i) {
                        float pos[3] = { 0.0F, 0.0F, 0.0F };
                        cgltf_accessor_read_float(accessor, i, pos, 3);
                        checksum += static_cast<double>(pos[0]);
                    }
                }
            }
        }
        cgltf_free(data);
        benchmark::DoNotOptimize(checksum);
    }
}

void BM_GltfParse_CubeGlb(benchmark::State &state) { parse_and_walk_gltf(find_model("GltfTest/cube.glb"), state); }
BENCHMARK(BM_GltfParse_CubeGlb)->Unit(benchmark::kMicrosecond);

void BM_GltfParse_CubeTextured(benchmark::State &state)
{
    parse_and_walk_gltf(find_model("GltfTest/cube_textured.gltf"), state);
}
BENCHMARK(BM_GltfParse_CubeTextured)->Unit(benchmark::kMicrosecond);

}// namespace

BENCHMARK_MAIN();
