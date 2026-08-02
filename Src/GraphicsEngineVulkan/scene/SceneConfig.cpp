module;
#include <cstdlib>

#include <filesystem>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <span>
#include <string>
#include <system_error>
#include <vector>

module kataglyphis.vulkan.scene_config;

import kataglyphis.vulkan.model_file_kind;
import kataglyphis.shared.util.resource_paths;

namespace sceneConfig {

auto resolveModelPath(const std::string &relativeModelPath) -> std::string
{
    if (const auto resolved = Kataglyphis::Shared::resolveResourceRelativePath(relativeModelPath);
        resolved.has_value()) {
        return resolved->string();
    }

    // Nothing matched: fall back to the build-relative candidate so callers
    // (ObjLoader/GltfLoader) still get a path back to log, rather than the
    // bare relative string.
    std::error_code filesystem_error;
    const std::filesystem::path current_path = std::filesystem::current_path(filesystem_error);
    if (filesystem_error) { return relativeModelPath; }

    return (std::filesystem::path(current_path.string() + RELATIVE_RESOURCE_PATH) / relativeModelPath).string();
}

namespace {
    auto findResourcesBasePath() -> std::filesystem::path { return Kataglyphis::Shared::resourcesRootOrEmpty(); }

    auto computeModelDisplayName(const std::string &relativePath) -> std::string
    {
        const std::filesystem::path path(relativePath);
        std::string display = path.stem().string();
        const std::filesystem::path parent = path.parent_path();
        if (parent.has_relative_path() && parent != "." && !parent.empty()) {
            display = parent.string() + "/" + display;
        }
        return display;
    }

    std::vector<std::string> s_cached_model_paths;
    std::vector<std::string> s_cached_model_display_names;
    bool s_models_scanned = false;

    void scanAvailableModels()
    {
        if (s_models_scanned) return;

        std::error_code ec;
        const std::filesystem::path resources_path = findResourcesBasePath();
        const std::filesystem::path models_dir = resources_path / "Models";

        if (!std::filesystem::exists(models_dir, ec) || ec) return;

        // s_models_scanned latches only once models_dir is confirmed
        // reachable - a call made before the working directory finds
        // Resources/Models must be free to retry on the next call instead of
        // reporting "No loadable models" for the rest of the process.
        s_models_scanned = true;

        std::filesystem::recursive_directory_iterator it(
          models_dir, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code entry_ec;
            if (!it->is_regular_file(entry_ec) || entry_ec) { continue; }
            if (!Kataglyphis::isSupportedModelPath(it->path().string())) { continue; }

            std::error_code rel_ec;
            const std::filesystem::path rel = std::filesystem::relative(it->path(), resources_path, rel_ec);
            if (rel_ec) { continue; }
            s_cached_model_paths.push_back(rel.string());
            s_cached_model_display_names.push_back(computeModelDisplayName(rel.string()));
        }
    }
}// namespace

auto getModelFile() -> std::string
{
    // Tests need a scene chosen for what it MEASURES, not for how it looks,
    // and the two are not the same choice. The debug scene below is a
    // dinosaur skeleton because it is pleasant to open the app on; its thin
    // bones make a poor shadow caster, and measuring cascaded shadows against
    // it reported 0.13% occlusion where the same renderer over a solid box
    // reports 6.45%. Without this hook the golden shadow test can only ever
    // assert a threshold low enough to be meaningless.
    if (const char *override_path = std::getenv("KATAGLYPHIS_MODEL_OVERRIDE"); override_path != nullptr) {
        if (*override_path != '\0') { return resolveModelPath(override_path); }
    }

    std::string relativeModelPath;

#if NDEBUG
    relativeModelPath = "Models/crytek-sponza/sponza_triag.obj";
#else
    // Dinosaurs is the default debug scene because it SHOWS the cascaded
    // shadow maps: it carries its own 20x20 ground plane at y=0 with the
    // figures standing up to y=3.64, so the shadows land on a visible floor
    // instead of only self-shadowing a single object.
    relativeModelPath = "Models/Dinosaurs/dinosaurs.obj";
#endif

    return resolveModelPath(relativeModelPath);
}

// Both configurations scale by (1,1,1), i.e. not at all. The function still
// exists because of its history: the old 60x scale for the tiny viking_room
// made the camera start INSIDE the geometry (all backfaces, culled -> black
// viewport) and stretched the scene far beyond a cascade's useful resolution.
// Dinosaurs (debug) and crytek-sponza (release) both need no scaling.
auto getModelMatrix() -> glm::mat4
{
    return glm::mat4(1.0F);
}

auto getAvailableModelPaths() -> std::span<const std::string>
{
    scanAvailableModels();
    return s_cached_model_paths;
}

auto getAvailableModelDisplayNames() -> std::span<const std::string>
{
    scanAvailableModels();
    return s_cached_model_display_names;
}

}// namespace sceneConfig
