module;

#include <glad/glad.h>

export module kataglyphis.opengl.repeat_mode;

import kataglyphis.opengl.texture_wrapping_mode;

export class RepeatMode final : public TextureWrappingMode
{
  public:
    RepeatMode();

    void activate() override;

    ~RepeatMode();
};
