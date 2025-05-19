#include "compute/ComputeShaderProgram.hpp"

ComputeShaderProgram::ComputeShaderProgram() {}

void ComputeShaderProgram::reload() {
  create_computer_shader_program_from_file(compute_location);
}

ComputeShaderProgram::~ComputeShaderProgram() {}
