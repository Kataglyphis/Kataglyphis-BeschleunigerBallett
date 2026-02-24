module;

module kataglyphis.opengl.omni_dir_shadow_shader_program;

import kataglyphis.opengl.shader_program;

OmniDirShadowShaderProgram::OmniDirShadowShaderProgram() = default;

void OmniDirShadowShaderProgram::reload()
{
    create_from_files(this->vertex_location, this->geometry_location, this->fragment_location);
}

OmniDirShadowShaderProgram::~OmniDirShadowShaderProgram() = default;
