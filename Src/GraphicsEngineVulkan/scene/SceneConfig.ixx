module;

#include <glm/glm.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module kataglyphis.vulkan.scene_config;

export namespace sceneConfig {
std::string getModelFile();
glm::mat4 getModelMatrix();
std::span<const std::string> getAvailableModelPaths();
std::span<const std::string> getAvailableModelDisplayNames();
std::string resolveModelPath(const std::string &relativeModelPath);
std::string_view defaultModelRelativePath();
int defaultSelectedModelIndex(std::span<const std::string> availablePaths, std::string_view preferredRelativePath);
}// namespace sceneConfig
