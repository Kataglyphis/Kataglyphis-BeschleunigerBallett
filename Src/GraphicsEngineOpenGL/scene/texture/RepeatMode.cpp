#include "scene/texture/RepeatMode.hpp"
#include <glad/glad.h>

RepeatMode::RepeatMode() = default;

void RepeatMode::activate()
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

RepeatMode::~RepeatMode() = default;
