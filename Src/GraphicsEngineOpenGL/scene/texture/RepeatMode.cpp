module;

#include <glad/glad.h>

module kataglyphis.opengl.repeat_mode;

RepeatMode::RepeatMode() = default;

void RepeatMode::activate()
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

RepeatMode::~RepeatMode() = default;
