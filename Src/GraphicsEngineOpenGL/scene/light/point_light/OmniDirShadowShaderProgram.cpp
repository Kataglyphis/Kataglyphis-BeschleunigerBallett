#include "OmniDirShadowShaderProgram.hpp"

OmniDirShadowShaderProgram::OmniDirShadowShaderProgram() {}

void OmniDirShadowShaderProgram::reload()
{
    create_from_files(this->vertex_location, this->geometry_location, this->fragment_location);
}

OmniDirShadowShaderProgram::~OmniDirShadowShaderProgram() {}
