#include "renderer/ShaderProgram.hpp"

#include <sstream>
#include <filesystem>

#include "util/File.hpp"
#include "renderer/OpenGLRendererConfig.hpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

ShaderProgram::ShaderProgram()
    :

      program_id(0),
      vertex_location(""),
      fragment_location(""),
      geometry_location(""),
      compute_location("fragment_location")

{
  std::stringstream aux;
  std::filesystem::path cwd = std::filesystem::current_path();
  aux << cwd.string();
  aux << RELATIVE_RESOURCE_PATH;
  aux << "Shaders/";

  shader_base_dir = aux.str();
}

void ShaderProgram::create_from_files(const char* vertex_location,
                                      const char* fragment_location) {
  std::stringstream vertex_shader;
  std::stringstream fragment_shader;
  vertex_shader << shader_base_dir << vertex_location;
  fragment_shader << shader_base_dir << fragment_location;

  File vertex_shader_file(vertex_shader.str());
  File fragment_shader_file(fragment_shader.str());

  std::string vertex_string = vertex_shader_file.read();
  std::string fragment_string = fragment_shader_file.read();

  // we need c-like strings ....
  const char* vertex_code = vertex_string.c_str();
  const char* fragment_code = fragment_string.c_str();

  this->vertex_location = (vertex_location);
  this->fragment_location = (fragment_location);

  compile_shader_program(vertex_code, fragment_code);
}

void ShaderProgram::create_from_files(const char* vertex_location,
                                      const char* geometry_location,
                                      const char* fragment_location) {
  std::stringstream vertex_shader;
  std::stringstream geometry_shader;
  std::stringstream fragment_shader;
  vertex_shader << shader_base_dir << vertex_location;
  geometry_shader << shader_base_dir << geometry_location;
  fragment_shader << shader_base_dir << fragment_location;

  File vertex_shader_file(vertex_shader.str());
  File geometry_shader_file(geometry_shader.str());
  File fragment_shader_file(fragment_shader.str());

  std::string vertex_string = vertex_shader_file.read();
  std::string geometry_string = geometry_shader_file.read();
  std::string fragment_string = fragment_shader_file.read();

  const char* vertex_code = vertex_string.c_str();
  const char* geometry_code = geometry_string.c_str();
  const char* fragment_code = fragment_string.c_str();

  this->vertex_location = vertex_location;
  this->fragment_location = fragment_location;
  this->geometry_location = geometry_location;

  compile_shader_program(vertex_code, geometry_code, fragment_code);
}

void ShaderProgram::create_computer_shader_program_from_file(
    const char* compute_location) {
  std::stringstream comp_shader;
  comp_shader << shader_base_dir << compute_location;
  File compute_shader_file(comp_shader.str());
  std::string file = compute_shader_file.read();

  const char* compute_code = file.c_str();

  this->compute_location = compute_location;

  compile_compute_shader_program(compute_code);
}

GLuint ShaderProgram::get_id() const { return program_id; }

void ShaderProgram::validate_program() {
  GLint result = 0;
  GLchar eLog[1024] = {0};

  glValidateProgram(program_id);

  glGetProgramiv(program_id, GL_VALIDATE_STATUS, &result);

  if (!result) {
    glGetProgramInfoLog(program_id, sizeof(eLog), NULL, eLog);
    printf("Error validating program: '%s'\n", eLog);
    return;
  }
}

void ShaderProgram::use_shader_program() { glUseProgram(program_id); }

void ShaderProgram::add_shader(GLuint program, const char* shader_code,
                               GLenum shader_type) {
  GLuint shader = glCreateShader(shader_type);

  // the opengl function wants c -style char array of code and the length in an
  // array ... so we do it
  const GLchar* code[1];
  code[0] = shader_code;

  GLint code_length[1];
  code_length[0] = (GLint)strlen(shader_code);

  glShaderSource(shader, 1, code, code_length);
  glCompileShader(shader);
  // glCompileShaderIncludeARB(shader);

  GLint result = 0;
  GLchar eLog[1024] = {0};

  // retrieve status of the shader and print if any error occured
  glGetShaderiv(shader, GL_COMPILE_STATUS, &result);

  if (!result) {
    glGetShaderInfoLog(shader, sizeof(eLog), NULL, eLog);
    printf("Error compiling the %d shader:  '%s'\n", shader_type, eLog);
    printf("%s", shader_code);
    return;
  }

  // we are happy, everything went well so attach shader to program
  glAttachShader(program, shader);
}

void ShaderProgram::compile_shader_program(const char* vertex_code,
                                           const char* fragment_code) {
  // retrieve the id; we need to reference it later on
  program_id = glCreateProgram();

  if (!program_id) {
    printf("Error creating shader program !\n");
    return;
  }
  // we will always need one vertex ShaderProgram
  add_shader(program_id, vertex_code, GL_VERTEX_SHADER);
  // and one fragment ShaderProgram
  add_shader(program_id, fragment_code, GL_FRAGMENT_SHADER);

  // we attached all shaders
  // so compile program
  compile_program();
}

void ShaderProgram::compile_shader_program(const char* vertex_code,
                                           const char* geometry_code,
                                           const char* fragment_code) {
  program_id = glCreateProgram();

  if (!program_id) {
    printf("Error creating shader program!\n");
    return;
  }

  add_shader(program_id, vertex_code, GL_VERTEX_SHADER);
  add_shader(program_id, geometry_code, GL_GEOMETRY_SHADER);
  add_shader(program_id, fragment_code, GL_FRAGMENT_SHADER);

  compile_program();
}

void ShaderProgram::compile_compute_shader_program(const char* compute_code) {
  program_id = glCreateProgram();

  if (!program_id) {
    printf("Error creating shader program!\n");
    return;
  }

  add_shader(program_id, compute_code, GL_COMPUTE_SHADER);

  compile_program();
}

void ShaderProgram::compile_program() {
  // as simple as that; opengl will link it for us :)
  glLinkProgram(program_id);
  GLint result = 0;
  GLchar eLog[1024] = {0};

  glGetProgramiv(program_id, GL_LINK_STATUS, &result);

  if (!result) {
    glGetProgramInfoLog(program_id, sizeof(eLog), NULL, eLog);
    printf("Error linking program: '%s'\n", eLog);
    return;
  }

  validate_program();
}

bool ShaderProgram::setUniformVec3(glm::vec3 uniform,
                                   const std::string& shaderUniformName) {
  bool validity = true;
  GLuint uniform_location = getUniformLocation(shaderUniformName, validity);

  if (validity) {
    glUniform3f(uniform_location, uniform.x, uniform.y, uniform.z);
  }

  return validity;
}

bool ShaderProgram::setUniformFloat(GLfloat uniform,
                                    const std::string& shaderUniformName) {
  bool validity = true;
  GLuint uniform_location = getUniformLocation(shaderUniformName, validity);

  if (validity) {
    glUniform1f(uniform_location, uniform);
  }

  return validity;
}

bool ShaderProgram::setUniformInt(GLint uniform,
                                  const std::string& shaderUniformName) {
  bool validity = true;
  GLuint uniform_location = getUniformLocation(shaderUniformName, validity);

  if (validity) {
    glUniform1i(uniform_location, uniform);
  }

  return validity;
}

bool ShaderProgram::setUniformMatrix4fv(glm::mat4 matrix,
                                        const std::string& shaderUniformName) {
  bool validity = true;
  GLuint uniform_location = getUniformLocation(shaderUniformName, validity);

  if (validity) {
    glUniformMatrix4fv(uniform_location, 1, GL_FALSE, glm::value_ptr(matrix));
  }

  return validity;
}

bool ShaderProgram::setUniformBlockBinding(
    GLuint block_binding, const std::string& shaderUniformName) {
  bool validity = true;
  GLint uniform_location =
      glGetUniformBlockIndex(program_id, shaderUniformName.c_str());

  (uniform_location < 0) ? validity = false : validity = true;

  if (validity) {
    glUniformBlockBinding(program_id, uniform_location, block_binding);
  } else {
#ifdef NDEBUG
    // nondebug

#else
    // printf("Error at setting uniform block binding!");
#endif
  }

  return validity;
}

bool ShaderProgram::validateUniformLocation(GLint uniformLocation) {
  // if uniform location is invalid (f.e. var disappears because of optimizing
  // of unused vars)
  return (uniformLocation == -1) ? false : true;
}

GLuint ShaderProgram::getUniformLocation(const std::string& shaderUniformName,
                                         bool& validity) {
  GLuint uniform_location =
      glGetUniformLocation(program_id, shaderUniformName.c_str());
  validity = validateUniformLocation(uniform_location);

#ifdef NDEBUG
  // nondebug

#else

  if (!validity) {
    /*std::stringstream ss;
            ss << "You have set a wrong uniform! "
                << "Name: " << shaderUniformName << "\n";

            std::cout << ss.str();*/
  }

#endif

  return uniform_location;
}

void ShaderProgram::clear_shader_program() {
  // don't trash the id's!!
  // delete it from memory!!
  if (program_id != 0) {
    glDeleteProgram(program_id);
    program_id = 0;
  }
}

ShaderProgram::~ShaderProgram() { clear_shader_program(); }
