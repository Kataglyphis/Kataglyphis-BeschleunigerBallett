module;

#include <glad/glad.h>

export module kataglyphis.opengl.texture_wrapping_mode;

export class TextureWrappingMode
{
  public:
    virtual ~TextureWrappingMode() = default;
    virtual void activate() = 0;
};
