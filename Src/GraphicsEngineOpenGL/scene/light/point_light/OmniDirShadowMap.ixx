module;
#include <glad/glad.h>

export module kataglyphis.opengl.point_light.omni_dir_shadow_map;

import kataglyphis.opengl.shadows.shadow_map;

export class OmniDirShadowMap : public ShadowMap
{
  public:
    OmniDirShadowMap();

    bool init(GLuint width, GLuint height) override;

    void write() override;

    void read(GLenum texture_unit) override;

    ~OmniDirShadowMap() override;
};
