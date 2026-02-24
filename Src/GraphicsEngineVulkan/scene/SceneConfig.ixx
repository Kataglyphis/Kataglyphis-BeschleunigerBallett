module;

#include <glm/glm.hpp>
#include <string>

export module kataglyphis.vulkan.scene_config;

export namespace sceneConfig {
std::string getModelFile();
glm::mat4 getModelMatrix();
}// namespace sceneConfig
