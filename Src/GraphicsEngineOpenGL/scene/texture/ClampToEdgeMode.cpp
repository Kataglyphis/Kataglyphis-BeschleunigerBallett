#include "ClampToEdgeMode.hpp"

ClampToEdgeMode::ClampToEdgeMode() {}

void ClampToEdgeMode::activate()
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

ClampToEdgeMode::~ClampToEdgeMode() {}
