#include <iomanip>
#include <sstream>

#include "common/Utilities.hpp"
#include "renderer/VulkanRendererConfig.hpp"
#include "vulkan_base/ShaderHelper.hpp"
#include "vulkan_base/ShaderIncludes.hpp"

#include "spdlog/spdlog.h"

ShaderHelper::ShaderHelper() {}

void ShaderHelper::compileShader(const std::string &shader_src_dir, const std::string &shader_name)
{
    // GLSLC_EXE is set by cmake to the location of the vulkan glslc
    std::stringstream shader_src_path;
    std::stringstream shader_log_file;
    std::stringstream cmdShaderCompile;
    std::stringstream adminPriviliges;
    adminPriviliges << "runas /user:<admin-user> \"";

    // with wrapping your path with quotation marks one can use paths with blanks ...
    shader_src_path << shader_src_dir << shader_name;
    std::string shader_spv_path = getShaderSpvDir(shader_src_dir, shader_name);
    shader_log_file << shader_src_dir << shader_name << ".log.txt";
    std::stringstream log_stdout_and_stderr;
    log_stdout_and_stderr << " > " << shader_log_file.str() << " 2> " << shader_log_file.str();

    cmdShaderCompile//<< adminPriviliges.str()
      << GLSLC_EXE << target << std::quoted(shader_src_path.str()) << " -o " << std::quoted(shader_spv_path)
      << ShaderIncludes::getShaderIncludes();
    //<< log_stdout_and_stderr.str();

    spdlog::info("The shader compile command is the following: {}", cmdShaderCompile.str());
    // std::cout << cmdShaderCompile.str().c_str();

    system(cmdShaderCompile.str().c_str());
}

std::string ShaderHelper::getShaderSpvDir(const std::string &shader_src_dir, const std::string &shader_name)
{
    std::string shader_spv_dir = "spv/";

    std::stringstream vertShaderSpv;
    vertShaderSpv << shader_src_dir << shader_spv_dir << shader_name << ".spv";

    return vertShaderSpv.str();
}

VkShaderModule ShaderHelper::createShaderModule(VulkanDevice *device, const std::vector<char> &code)
{
    // shader module create info
    VkShaderModuleCreateInfo shader_module_create_info{};
    shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_module_create_info.codeSize = code.size();// size of code
    shader_module_create_info.pCode = reinterpret_cast<const uint32_t *>(code.data());// pointer to code

    VkShaderModule shader_module;
    VkResult result =
      vkCreateShaderModule(device->getLogicalDevice(), &shader_module_create_info, nullptr, &shader_module);

    ASSERT_VULKAN(result, "Failed to create a shader module!")

    return shader_module;
}

ShaderHelper::~ShaderHelper() {}
