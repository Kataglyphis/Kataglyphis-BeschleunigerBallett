#include "MirroredRepeatMode.hpp"

MirroredRepeatMode::MirroredRepeatMode() {}

void MirroredRepeatMode::activate()
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
}

MirroredRepeatMode::~MirroredRepeatMode() {}
