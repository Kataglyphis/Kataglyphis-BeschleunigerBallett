module;

#include <glad/glad.h>

export module kataglyphis.opengl.clamp_to_edge_mode;

import kataglyphis.opengl.texture_wrapping_mode;

export class ClampToEdgeMode : public TextureWrappingMode
{
  public:
    ClampToEdgeMode();

    void activate() override;

    ~ClampToEdgeMode();
};
