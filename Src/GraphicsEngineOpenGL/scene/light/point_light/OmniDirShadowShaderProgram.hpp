#pragma once
#include "renderer/ShaderProgram.hpp"

class OmniDirShadowShaderProgram : public ShaderProgram {
 public:
  OmniDirShadowShaderProgram();

  void reload();

  ~OmniDirShadowShaderProgram();

 private:
};
