module;

#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "renderer/VulkanRendererConfig.hpp"

export module kataglyphis.vulkan.shader_helper;

import kataglyphis.vulkan.device;

export namespace Kataglyphis {
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
