module;
#include <memory>

#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.shader_helper;

import kataglyphis.vulkan.device;

export namespace Kataglyphis {
class ShaderHelper
{
  public:
    ShaderHelper();

    static std::string getShaderSpvDir(const std::string &shader_src_dir, const std::string &shader_name);

    static vk::ShaderModule createShaderModule(std::shared_ptr<VulkanDevice>device, const std::vector<char> &code);

    ~ShaderHelper();
};
}// namespace Kataglyphis
