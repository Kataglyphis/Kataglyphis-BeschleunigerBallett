#include "SceneConfig.hpp"
#include "renderer/VulkanRendererConfig.hpp"

#include <filesystem>
#include <sstream>
// #define SULO_MODE 1

namespace sceneConfig {

std::string getModelFile()
{
    std::stringstream modelFile;
    std::filesystem::path cwd = std::filesystem::current_path();
    modelFile << cwd.string();
    modelFile << RELATIVE_RESOURCE_PATH;

#if NDEBUG
    modelFile << "Models/crytek-sponza/";
    modelFile << "sponza_triag.obj";

#else
#ifdef SULO_MODE
    modelFile << "Model/Sulo/WolfStahl/";
    // modelFile << "Wolf-Stahl.obj";
    modelFile << "SuloLongDongLampe_v2.obj";
#else
    modelFile << "Models/VikingRoom/";
    modelFile << "viking_room.obj";
#endif
#endif

    return modelFile.str();
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

glm::mat4 getModelMatrix()
{
    glm::mat4 modelMatrix(1.0f);

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
    modelMatrix = glm::scale(modelMatrix, glm::vec3(60.0f, 60.0f, 60.0f));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(-90.f), glm::vec3(1.0f, 0.0f, 0.0f));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(90.f), glm::vec3(0.0f, 0.0f, 1.0f));
#endif

#endif

    return modelMatrix;
}

}// namespace sceneConfig
