module;

module kataglyphis.opengl.compute_shader_program;

import kataglyphis.opengl.shader_program;

ComputeShaderProgram::ComputeShaderProgram() = default;

void ComputeShaderProgram::reload() { create_computer_shader_program_from_file(compute_location); }

ComputeShaderProgram::~ComputeShaderProgram() = default;
