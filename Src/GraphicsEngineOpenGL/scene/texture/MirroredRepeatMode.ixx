module;

#include <glad/glad.h>

export module kataglyphis.opengl.mirrored_repeat_mode;

import kataglyphis.opengl.texture_wrapping_mode;

export class MirroredRepeatMode : public TextureWrappingMode
{
  public:
    MirroredRepeatMode();

    void activate() override;

    virtual ~MirroredRepeatMode();
};
