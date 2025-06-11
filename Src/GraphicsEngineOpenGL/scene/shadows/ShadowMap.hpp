#pragma once

#include <glad/glad.h>

class ShadowMap
{
  public:
    ShadowMap();

    virtual bool init(GLuint width, GLuint height);
    virtual void write();
    virtual void read(GLenum texture_unit);

    GLuint get_shadow_width() const { return shadow_width; };
    GLuint get_shadow_height() const { return shadow_height; };
    GLuint get_id() const { return shadow_map; };

    virtual ~ShadowMap();

  protected:
    GLuint FBO, shadow_map;
    GLuint shadow_width, shadow_height;
};
