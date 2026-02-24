module;

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <glad/glad.h>
#include <sstream>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"
#include "renderer/OpenGLRendererConfig.hpp"

module kataglyphis.opengl.shader_includes;

import kataglyphis.opengl.file;

ShaderIncludes::ShaderIncludes()
{
    assert(includeNames.size() == file_locations_relative.size());

    // Check if the extension is supported via glad
    // The previous check was: if (GLAD_GL_ARB_shading_language_include == 0 || glNamedStringARB == nullptr)
    // However, glNamedStringARB is a function pointer loaded by glad, so we should check if it's not null.
    // Also check the integer flag provided by glad.
    if (!GLAD_GL_ARB_shading_language_include) {
        spdlog::warn(
          "GL_ARB_shading_language_include is not available on this OpenGL driver/context. "
          "Shader include registration is skipped.");
        return;
    }

    spdlog::info("GL_ARB_shading_language_include is available.");

    std::vector<std::string> file_locations_abs;
    for (uint32_t i = 0; i < static_cast<uint32_t>(includeNames.size()); i++) {
        std::stringstream aux;
        std::filesystem::path const cwd = std::filesystem::current_path();
        aux << cwd.string();
        aux << RELATIVE_RESOURCE_PATH;
        aux << file_locations_relative[i];
        file_locations_abs.push_back(aux.str());
    }

    for (uint32_t i = 0; i < static_cast<uint32_t>(includeNames.size()); i++) {
        File file(file_locations_abs[i]);
        std::string const file_content = file.read();
        char tmpstr[2000];
        snprintf(tmpstr, 2000, "/%s", includeNames[i]);
        if (glNamedStringARB) {
            glNamedStringARB(GL_SHADER_INCLUDE_ARB,
              static_cast<GLint>(strlen(tmpstr)),
              tmpstr,
              static_cast<GLint>(strlen(file_content.c_str())),
              file_content.c_str());
        }
    }
}

ShaderIncludes::~ShaderIncludes() = default;
