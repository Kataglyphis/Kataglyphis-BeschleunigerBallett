#pragma once
#include "TextureWrappingMode.hpp"
class RepeatMode final : public TextureWrappingMode
{
  public:
    RepeatMode();

    void activate() override;

    ~RepeatMode();

  private:
};
