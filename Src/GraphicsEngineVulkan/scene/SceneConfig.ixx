module;

#include <glm/glm.hpp>
#include <string>
#include <vector>

export module kataglyphis.vulkan.scene_config;

export namespace sceneConfig {
std::string getModelFile();
glm::mat4 getModelMatrix();
std::vector<std::string> getAvailableModelPaths();
std::vector<std::string> getAvailableModelDisplayNames();
std::string resolveModelPath(const std::string &relativeModelPath);
}// namespace sceneConfig
