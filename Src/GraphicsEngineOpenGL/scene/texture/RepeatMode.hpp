#pragma once
#include "TextureWrappingMode.hpp"
class RepeatMode : public TextureWrappingMode
{
  public:
    RepeatMode();

    void activate() override;

    ~RepeatMode();

  private:
};
