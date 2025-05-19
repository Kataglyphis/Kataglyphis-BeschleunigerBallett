#pragma once
#include <vulkan/vulkan.h>

#include <string>
#include <vector>

#include "vulkan_base/VulkanDevice.hpp"

class ShaderHelper
{
  public:
    ShaderHelper();

    void compileShader(const std::string &shader_src_dir, const std::string &shader_name);
    std::string getShaderSpvDir(const std::string &shader_src_dir, const std::string &shader_name);

    VkShaderModule createShaderModule(VulkanDevice *device, const std::vector<char> &code);

    ~ShaderHelper();

  private:
    std::string target = " --target-env=vulkan1.3 ";
};
