#include "renderer/ShaderIncludes.hpp"
#include "renderer/OpenGLRendererConfig.hpp"

// clang-format off
// you must include glad before glfw!
// therefore disable clang-format for this section
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <stdio.h>
#include <string.h>

#include <array>
#include <cassert>
#include <filesystem>
#include <sstream>

#include "spdlog/spdlog.h"
#include "util/File.hpp"

// this method is setting all files we want to use in a shader per #include
// you have to specify the name(how file appears in shader)
// and its actual file location relatively
// https://www.khronos.org/registry/OpenGL/extensions/ARB/ARB_shading_language_include.txt
ShaderIncludes::ShaderIncludes()
{
    assert(includeNames.size() == file_locations_relative.size());

    if (!isExtensionSupported("GL_ARB_shading_language_include")) {
        spdlog::error("GL_ARB_shading_language_include is supported!");
    }

    std::vector<std::string> file_locations_abs;
    for (uint32_t i = 0; i < static_cast<uint32_t>(includeNames.size()); i++) {
        std::stringstream aux;
        std::filesystem::path cwd = std::filesystem::current_path();
        aux << cwd.string();
        aux << RELATIVE_RESOURCE_PATH;
        aux << file_locations_relative[i];
        file_locations_abs.push_back(aux.str());
    }

    for (uint32_t i = 0; i < static_cast<uint32_t>(includeNames.size()); i++) {
        File file(file_locations_abs[i]);
        std::string file_content = file.read();
        char tmpstr[2000];
        snprintf(tmpstr, 2000, "/%s", includeNames[i]);
        if (glNamedStringARB) {
            spdlog::info("glNamedStringARB successfully loaded!");
        } else {
            spdlog::error("Failed to load glNamedStringARB!");
        }
        glNamedStringARB(GL_SHADER_INCLUDE_ARB,
          static_cast<GLint>(strlen(tmpstr)),
          tmpstr,
          static_cast<GLint>(strlen(file_content.c_str())),
          file_content.c_str());
    }
}

ShaderIncludes::~ShaderIncludes() {}
