#include "ClampToEdgeMode.hpp"
#include <glad/glad.h>

ClampToEdgeMode::ClampToEdgeMode() = default;

void ClampToEdgeMode::activate()
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

ClampToEdgeMode::~ClampToEdgeMode() = default;
