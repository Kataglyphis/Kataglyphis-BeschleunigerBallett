module;

#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.h>

export module kataglyphis.vulkan.shader_helper;

import kataglyphis.vulkan.config;
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
    std::string target = std::string(" --target-env=vulkan")
      + std::string(Kataglyphis::RendererConfig::vulkanVersionMajor) + "."
      + std::string(Kataglyphis::RendererConfig::vulkanVersionMinor) + " ";
};
}// namespace Kataglyphis
