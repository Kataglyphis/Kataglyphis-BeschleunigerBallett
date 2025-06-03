#pragma once
#include "TextureWrappingMode.hpp"

class MirroredRepeatMode : public TextureWrappingMode
{
  public:
    MirroredRepeatMode();

    void activate() override;

    ~MirroredRepeatMode();

  private:
};
