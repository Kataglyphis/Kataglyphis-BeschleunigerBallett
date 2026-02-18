#pragma once
#include <vulkan/vulkan.h>

#include <string>
#include <vector>

#include "renderer/VulkanRendererConfig.hpp"
#include "vulkan_base/VulkanDevice.hpp"
namespace Kataglyphis {
class ShaderHelper
{
  public:
    ShaderHelper();

    void compileShader(const std::string &shader_src_dir, const std::string &shader_name);
    static std::string getShaderSpvDir(const std::string &shader_src_dir, const std::string &shader_name);

    static VkShaderModule createShaderModule(VulkanDevice *device, const std::vector<char> &code);

    ~ShaderHelper();

  private:
    std::string target = " --target-env=vulkan" VULKAN_VERSION_MAJOR "." VULKAN_VERSION_MINOR " ";
};
}// namespace Kataglyphis