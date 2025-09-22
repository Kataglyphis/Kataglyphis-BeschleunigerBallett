#pragma once

#include <glad/glad.h>
#include <string>
#include <vector>

// https://www.khronos.org/registry/OpenGL/extensions/ARB/ARB_shading_language_include.txt
class ShaderIncludes
{
  public:
    ShaderIncludes();

    ~ShaderIncludes();

  private:
    bool isExtensionSupported(const std::string &extension)
    {
        GLint numExtensions = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);

        for (int i = 0; i < numExtensions; ++i) {
            const char *ext = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i));
            if (extension == ext) { return true; }
        }
        return false;
    }
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
};
