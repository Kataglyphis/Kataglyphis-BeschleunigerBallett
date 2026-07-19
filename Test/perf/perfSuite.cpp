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

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

import kataglyphis.vulkan.camera;
import kataglyphis.vulkan.scene_config;

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

}// namespace

BENCHMARK_MAIN();
