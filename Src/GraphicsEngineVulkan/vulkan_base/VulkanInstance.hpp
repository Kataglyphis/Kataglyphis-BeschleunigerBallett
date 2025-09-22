#pragma once
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
namespace Kataglyphis {
class VulkanInstance
{
  public:
    VulkanInstance();

    VkInstance &getVulkanInstance() { return instance; };

    void cleanUp();

    ~VulkanInstance();

  private:
    VkInstance instance;

    // use the standard validation layers from the SDK for error checking
    std::vector<const char *> validationLayers = { "VK_LAYER_KHRONOS_validation" };

    bool check_validation_layer_support();
    bool check_instance_extension_support(std::vector<const char *> *check_extensions);
};
}// namespace Kataglyphis