#include "renderer/ShaderIncludes.hpp"

// clang-format off
// you must include glad before glfw!
// therefore disable clang-format for this section
#include <cstdint>
#include <glad/glad.h>
// clang-format on

#include <cstdio>
#include <cstring>

#include <cassert>
#include <filesystem>
#include <sstream>
#include <vector>

#include "spdlog/spdlog.h"
#include "util/File.hpp"

// this method is setting all files we want to use in a shader per #include
// you have to specify the name(how file appears in shader)
// and its actual file location relatively
// https://www.khronos.org/registry/OpenGL/extensions/ARB/ARB_shading_language_include.txt
ShaderIncludes::ShaderIncludes()
{
    assert(includeNames.size() == file_locations_relative.size());

    if (GLAD_GL_ARB_shading_language_include == 0 || glNamedStringARB == nullptr) {
        spdlog::warn("GL_ARB_shading_language_include is not available on this OpenGL driver/context. "
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
        glNamedStringARB(GL_SHADER_INCLUDE_ARB,
          static_cast<GLint>(strlen(tmpstr)),
          tmpstr,
          static_cast<GLint>(strlen(file_content.c_str())),
          file_content.c_str());
    }
}

ShaderIncludes::~ShaderIncludes() = default;
