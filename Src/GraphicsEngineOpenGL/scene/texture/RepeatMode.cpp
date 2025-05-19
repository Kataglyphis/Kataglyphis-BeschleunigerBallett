#include "scene/texture/RepeatMode.hpp"

RepeatMode::RepeatMode() {}

void RepeatMode::activate() {
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

RepeatMode::~RepeatMode() {}
