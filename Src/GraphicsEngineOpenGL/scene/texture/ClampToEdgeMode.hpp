#pragma once
#include "TextureWrappingMode.hpp"

class ClampToEdgeMode : public TextureWrappingMode
{
  public:
    ClampToEdgeMode();

    void activate() override;

    ~ClampToEdgeMode();

  private:
};
