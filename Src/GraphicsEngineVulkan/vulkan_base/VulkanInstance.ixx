module;

#include <vector>
#include <vulkan/vulkan.h>

export module kataglyphis.vulkan.instance;

export namespace Kataglyphis {
class VulkanInstance
{
  public:
    VulkanInstance();

    VkInstance &getVulkanInstance() { return instance; };

    void cleanUp();

    ~VulkanInstance();

  private:
    VkInstance instance{};

    std::vector<const char *> validationLayers = { "VK_LAYER_KHRONOS_validation" };

    bool check_validation_layer_support();
    static bool check_instance_extension_support(std::vector<const char *> *check_extensions);
};
}// namespace Kataglyphis
