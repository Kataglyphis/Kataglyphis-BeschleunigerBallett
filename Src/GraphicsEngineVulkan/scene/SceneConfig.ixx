module;

#include <filesystem>
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

/// Result of one walk of a Models directory. `complete` is false iff the walk
/// could not be started or was aborted by an error partway through - a false
/// `complete` means the caller must be free to retry, not treat `paths` as
/// the final answer.
struct ModelScanResult
{
    std::vector<std::string> paths;
    std::vector<std::string> displayNames;
    bool complete;
};

/// Walks `resourcesRoot / "Models"` and collects every supported model file
/// found underneath it. Does not touch any cache - see scanAvailableModels().
ModelScanResult scanModelsUnder(const std::filesystem::path &resourcesRoot);
}// namespace sceneConfig
