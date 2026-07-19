module;

#include <algorithm>
#include <filesystem>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <string>
#include <system_error>
#include <vector>
// #define SULO_MODE 1

module kataglyphis.vulkan.scene_config;

namespace sceneConfig {

auto resolveModelPath(const std::string &relativeModelPath) -> std::string
{
    std::error_code filesystem_error;
    const std::filesystem::path current_path = std::filesystem::current_path(filesystem_error);
    if (filesystem_error) { return relativeModelPath; }

    const std::filesystem::path direct_candidate =
      std::filesystem::path(current_path.string() + RELATIVE_RESOURCE_PATH) / relativeModelPath;
    if (std::filesystem::exists(direct_candidate, filesystem_error)) { return direct_candidate.string(); }

    auto search_path = current_path;
    constexpr int kModelSearchDepth = 8;
    for (int depth = 0; depth < kModelSearchDepth; ++depth) {
        const std::filesystem::path candidate = search_path / "Resources" / relativeModelPath;
        if (std::filesystem::exists(candidate, filesystem_error)) { return candidate.string(); }

        if (filesystem_error || !search_path.has_parent_path()) { break; }

        search_path = search_path.parent_path();
    }

    return direct_candidate.string();
}

namespace {
    auto findResourcesBasePath() -> std::filesystem::path
    {
        std::error_code filesystem_error;
        const std::filesystem::path current_path = std::filesystem::current_path(filesystem_error);

        const std::filesystem::path direct_candidate =
          std::filesystem::path(current_path.string() + RELATIVE_RESOURCE_PATH);
        if (std::filesystem::exists(direct_candidate / "Models", filesystem_error)) {
            return direct_candidate;
        }

        auto search_path = current_path;
        constexpr int kModelSearchDepth = 8;
        for (int depth = 0; depth < kModelSearchDepth; ++depth) {
            const std::filesystem::path candidate = search_path / "Resources";
            if (std::filesystem::exists(candidate / "Models", filesystem_error)) {
                return candidate;
            }
            if (filesystem_error || !search_path.has_parent_path()) { break; }
            search_path = search_path.parent_path();
        }

        return direct_candidate;
    }

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
        s_models_scanned = true;

        std::error_code ec;
        const std::filesystem::path resources_path = findResourcesBasePath();
        const std::filesystem::path models_dir = resources_path / "Models";

        if (!std::filesystem::exists(models_dir, ec)) return;

        for (const auto &entry : std::filesystem::recursive_directory_iterator(models_dir, ec)) {
            if (ec) break;
            if (entry.is_regular_file() && entry.path().extension() == ".obj") {
                const std::filesystem::path rel = std::filesystem::relative(entry.path(), resources_path);
                s_cached_model_paths.push_back(rel.string());
                s_cached_model_display_names.push_back(computeModelDisplayName(rel.string()));
            }
        }
    }
}// namespace

auto getModelFile() -> std::string
{
    std::string relativeModelPath;

#if NDEBUG
    relativeModelPath = "Models/crytek-sponza/sponza_triag.obj";

#else
#ifdef SULO_MODE
    relativeModelPath = "Model/Sulo/WolfStahl/SuloLongDongLampe_v2.obj";
#else
    // Dinosaurs is the default debug scene because it SHOWS the cascaded
    // shadow maps: it carries its own 20x20 ground plane at y=0 with the
    // figures standing up to y=3.64, so the shadows land on a visible floor
    // instead of only self-shadowing a single object.
    relativeModelPath = "Models/Dinosaurs/dinosaurs.obj";
#endif
#endif

    return resolveModelPath(relativeModelPath);
    // std::string modelFile =
    // "Models/crytek-sponza/sponza_triag.obj"; std::string modelFile
    // = "Models/Dinosaurs/dinosaurs.obj"; std::string modelFile =
    // "Models/Pillum/PilumPainting_export.obj"; std::string modelFile
    // = "Models/sibenik/sibenik.obj"; std::string modelFile =
    // "Models/sportsCar/sportsCar.obj"; std::string modelFile =
    // "Models/StanfordDragon/dragon.obj"; std::string modelFile =
    // "Models/CornellBox/CornellBox-Sphere.obj"; std::string
    // "Models/bunny/bunny.obj"; std::string modelFile =
    // "Models/buddha/buddha.obj"; std::string modelFile =
    // "Models/bmw/bmw.obj"; std::string modelFile =
    // "Models/testScene.obj"; std::string modelFile =
    // "Models/San_Miguel/san-miguel-low-poly.obj";
}

auto getModelMatrix() -> glm::mat4
{
    glm::mat4 modelMatrix(1.0F);

#if NDEBUG

    // dragon_model = glm::translate(dragon_model, glm::vec3(0.0f, -40.0f,
    // -50.0f));
    modelMatrix = glm::scale(modelMatrix, glm::vec3(1.0f, 1.0f, 1.0f));
    /*dragon_model = glm::rotate(dragon_model, glm::radians(-90.f),
       glm::vec3(1.0f, 0.0f, 0.0f)); dragon_model = glm::rotate(dragon_model,
       glm::radians(angle), glm::vec3(0.0f, 0.0f, 1.0f));*/

#else

// dragon_model = glm::translate(dragon_model, glm::vec3(0.0f, -40.0f,
// -50.0f));
#if SULO_MODE
    modelMatrix = glm::scale(modelMatrix, glm::vec3(60.0f, 60.0f, 60.0f));
#else
    // Dinosaurs is already ~20 units across, so it needs no scaling. The old
    // 60x scale existed for the tiny viking_room and made the camera start
    // INSIDE the geometry (all backfaces, culled -> black viewport), while
    // stretching the scene far beyond a cascade's useful resolution.
    modelMatrix = glm::scale(modelMatrix, glm::vec3(1.0F, 1.0F, 1.0F));
#endif

#endif

    return modelMatrix;
}

auto getAvailableModelPaths() -> std::vector<std::string>
{
    scanAvailableModels();
    return s_cached_model_paths;
}

auto getAvailableModelDisplayNames() -> std::vector<std::string>
{
    scanAvailableModels();
    return s_cached_model_display_names;
}

}// namespace sceneConfig
