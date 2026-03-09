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

module kataglyphis.opengl.shader_includes;

import kataglyphis.opengl.file;

namespace {
std::vector<const char *> includeNames = { "host_device_shared.hpp",
    "Matlib.glsl",
    "microfacet.glsl",
    "ShadingLibrary.glsl",
    "disney.glsl",
    "frostbite.glsl",
    "pbrBook.glsl",
    "phong.glsl",
    "unreal4.glsl",
    "clouds.glsl",
    "grad_noise.glsl",
    "worley_noise.glsl",
    "bindings.hpp",
    "GlobalValues.hpp",
    "directional_light.glsl",
    "light.glsl",
    "material.glsl",
    "point_light.glsl" };

std::vector<const char *> file_locations_relative = { "Shaders/hostDevice/host_device_shared.hpp",
    "Shaders/common/Matlib.glsl",
    "Shaders/pbr/microfacet.glsl",
    "Shaders/common/ShadingLibrary.glsl",
    "Shaders/pbr/brdf/disney.glsl",
    "Shaders/pbr/brdf/frostbite.glsl",
    "Shaders/pbr/brdf/pbrBook.glsl",
    "Shaders/pbr/brdf/phong.glsl",
    "Shaders/pbr/brdf/unreal4.glsl",
    "Shaders/clouds/clouds.glsl",
    "Shaders/common/grad_noise.glsl",
    "Shaders/common/worley_noise.glsl",
    "Shaders/hostDevice/bindings.hpp",
    "Shaders/hostDevice/GlobalValues.hpp",
    "Shaders/common/directional_light.glsl",
    "Shaders/common/light.glsl",
    "Shaders/common/material.glsl",
    "Shaders/common/point_light.glsl" };
}// namespace

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
