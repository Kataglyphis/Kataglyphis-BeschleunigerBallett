#include "SceneConfig.hpp"

#include <filesystem>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <string>
#include <system_error>
// #define SULO_MODE 1

namespace sceneConfig {

namespace {
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
    relativeModelPath = "Models/VikingRoom/viking_room.obj";
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
    modelMatrix = glm::scale(modelMatrix, glm::vec3(60.0F, 60.0F, 60.0F));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(-90.F), glm::vec3(1.0F, 0.0F, 0.0F));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(90.F), glm::vec3(0.0F, 0.0F, 1.0F));
#endif

#endif

    return modelMatrix;
}

}// namespace sceneConfig
