module;

#include <span>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.instance;

export namespace Kataglyphis {
class VulkanInstance
{
  public:
    VulkanInstance();

    vk::Instance &getVulkanInstance() { return instance; };

    void cleanUp();

    ~VulkanInstance();

  private:
    vk::Instance instance{};

    std::vector<const char *> validationLayers = { "VK_LAYER_KHRONOS_validation" };

    bool check_validation_layer_support();
    static bool check_instance_extension_support(std::span<const char *const> check_extensions);
};
}// namespace Kataglyphis
