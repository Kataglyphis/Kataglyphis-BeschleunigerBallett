module;

#include <glad/glad.h>

module kataglyphis.opengl.clamp_to_edge_mode;

ClampToEdgeMode::ClampToEdgeMode() = default;

void ClampToEdgeMode::activate()
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

ClampToEdgeMode::~ClampToEdgeMode() = default;
