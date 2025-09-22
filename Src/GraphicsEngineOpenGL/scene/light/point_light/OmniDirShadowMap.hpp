#pragma once
#include "scene/shadows/ShadowMap.hpp"

class OmniDirShadowMap : public ShadowMap
{
  public:
    OmniDirShadowMap();

    bool init(GLuint width, GLuint height);

    void write();

    void read(GLenum texture_unit);

    ~OmniDirShadowMap();
};
