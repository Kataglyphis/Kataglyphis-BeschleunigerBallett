module;

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>

#include <glad/glad.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/gtc/type_ptr.hpp>

module kataglyphis.opengl.shader_program;

import kataglyphis.opengl.file;

namespace {
auto trim(std::string value) -> std::string
{
    auto const first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) { return {}; }
    auto const last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

auto find_file_recursively(const std::filesystem::path &base_dir, const std::string &file_name)
  -> std::optional<std::filesystem::path>
{
    for (auto const &entry : std::filesystem::recursive_directory_iterator(base_dir)) {
        if (!entry.is_regular_file()) { continue; }
        if (entry.path().filename() == file_name) { return entry.path(); }
    }
    return std::nullopt;
}

auto detect_glsl_version_number() -> int
{
    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    if (major <= 0) { return 450; }

    int const requested_version = (major * 100) + (minor * 10);
    return std::clamp(requested_version, 330, 460);
}

auto preprocess_shader_source(const std::filesystem::path &shader_file,
  const std::filesystem::path &shader_root,
  std::unordered_set<std::string> &include_stack,
  bool &version_directive_written,
  int glsl_version_number) -> std::string
{
    std::string const canonical_key = std::filesystem::weakly_canonical(shader_file).string();
    if (include_stack.contains(canonical_key)) {
        std::cerr << "Detected recursive shader include: " << canonical_key << '\n';
        return {};
    }

    include_stack.insert(canonical_key);

    File shader_input(shader_file.string());
    std::string const source = shader_input.read();

    std::stringstream output;
    std::stringstream source_stream(source);
    std::string line;

    while (std::getline(source_stream, line)) {
        std::string const stripped = trim(line);

        if (stripped.starts_with("#extension GL_ARB_shading_language_include")) { continue; }

        if (stripped.starts_with("#version")) {
            if (!version_directive_written) {
                output << "#version " << glsl_version_number << '\n';
                version_directive_written = true;
            }
            continue;
        }

        if (stripped.starts_with("#include")) {
            auto const first_quote = stripped.find('"');
            auto const last_quote = stripped.find_last_of('"');

            if (first_quote != std::string::npos && last_quote != std::string::npos && last_quote > first_quote) {
                std::string include_target = stripped.substr(first_quote + 1, last_quote - first_quote - 1);

                std::filesystem::path include_path;

                if (!include_target.empty() && include_target[0] == '/') {
                    std::string const include_file_name = std::filesystem::path(include_target).filename().string();
                    auto const resolved = find_file_recursively(shader_root, include_file_name);
                    if (resolved.has_value()) { include_path = *resolved; }
                } else {
                    include_path = shader_file.parent_path() / include_target;
                    if (!std::filesystem::exists(include_path)) {
                        std::string const include_file_name = std::filesystem::path(include_target).filename().string();
                        auto const resolved = find_file_recursively(shader_root, include_file_name);
                        if (resolved.has_value()) { include_path = *resolved; }
                    }
                }

                if (!include_path.empty() && std::filesystem::exists(include_path)) {
                    output << preprocess_shader_source(
                      include_path, shader_root, include_stack, version_directive_written, glsl_version_number)
                           << '\n';
                    continue;
                }

                std::cerr << "Failed to resolve shader include '" << include_target << "' while processing '"
                          << shader_file.string() << "'." << '\n';
            }
        }

        output << line << '\n';
    }

    include_stack.erase(canonical_key);
    return output.str();
}

auto load_shader_source_with_includes(const std::filesystem::path &shader_file,
  const std::filesystem::path &shader_root) -> std::string
{
    std::unordered_set<std::string> include_stack;
    bool version_directive_written = false;
    return preprocess_shader_source(
      shader_file, shader_root, include_stack, version_directive_written, detect_glsl_version_number());
}
}// namespace

ShaderProgram::ShaderProgram()
  :

    program_id(0), vertex_location(""), fragment_location(""), geometry_location(""),
    compute_location("fragment_location")

{
    std::stringstream aux;
    std::filesystem::path const cwd = std::filesystem::current_path();
    aux << cwd.string();
    aux << RELATIVE_RESOURCE_PATH;
    aux << "Shaders/";

    shader_base_dir = aux.str();
}

void ShaderProgram::create_from_files(const char *vert_loc, const char *frag_loc)
{
    std::filesystem::path const shader_root(shader_base_dir);
    std::filesystem::path const vertex_shader = shader_root / vert_loc;
    std::filesystem::path const fragment_shader = shader_root / frag_loc;

    std::string const vertex_string = load_shader_source_with_includes(vertex_shader, shader_root);
    std::string const fragment_string = load_shader_source_with_includes(fragment_shader, shader_root);

    // we need c-like strings ....
    const char *vertex_code = vertex_string.c_str();
    const char *fragment_code = fragment_string.c_str();

    this->vertex_location = (vert_loc);
    this->fragment_location = (frag_loc);

    compile_shader_program(vertex_code, fragment_code);
}

void ShaderProgram::create_from_files(const char *vert_loc, const char *geom_loc, const char *frag_loc)
{
    std::filesystem::path const shader_root(shader_base_dir);
    std::filesystem::path const vertex_shader = shader_root / vert_loc;
    std::filesystem::path const geometry_shader = shader_root / geom_loc;
    std::filesystem::path const fragment_shader = shader_root / frag_loc;

    std::string const vertex_string = load_shader_source_with_includes(vertex_shader, shader_root);
    std::string const geometry_string = load_shader_source_with_includes(geometry_shader, shader_root);
    std::string const fragment_string = load_shader_source_with_includes(fragment_shader, shader_root);

    const char *vertex_code = vertex_string.c_str();
    const char *geometry_code = geometry_string.c_str();
    const char *fragment_code = fragment_string.c_str();

    this->vertex_location = vert_loc;
    this->fragment_location = frag_loc;
    this->geometry_location = geom_loc;

    compile_shader_program(vertex_code, geometry_code, fragment_code);
}

void ShaderProgram::create_computer_shader_program_from_file(const char *comp_loc)
{
    std::filesystem::path const shader_root(shader_base_dir);
    std::filesystem::path const comp_shader = shader_root / comp_loc;
    std::string const file = load_shader_source_with_includes(comp_shader, shader_root);

    const char *compute_code = file.c_str();

    this->compute_location = comp_loc;

    compile_compute_shader_program(compute_code);
}

auto ShaderProgram::get_id() const -> GLuint { return program_id; }

void ShaderProgram::validate_program() const
{
    GLint result = 0;
    GLchar eLog[1024] = { 0 };

    glValidateProgram(program_id);

    glGetProgramiv(program_id, GL_VALIDATE_STATUS, &result);

    if (result == 0) {
        glGetProgramInfoLog(program_id, sizeof(eLog), nullptr, eLog);
        std::cerr << "Error validating program: '" << eLog << "'" << '\n';
        return;
    }
}

void ShaderProgram::use_shader_program() const
{
    if (!program_is_linked) { return; }
    glUseProgram(program_id);
}

void ShaderProgram::add_shader(GLuint program, const char *shader_code, GLenum shader_type)
{
    GLuint const shader = glCreateShader(shader_type);

    // the opengl function wants c -style char array of code and the length in an
    // array ... so we do it
    const GLchar *code[1];
    code[0] = shader_code;

    GLint code_length[1];
    code_length[0] = static_cast<GLint>(strlen(shader_code));

    glShaderSource(shader, 1, code, code_length);
    glCompileShader(shader);
    // glCompileShaderIncludeARB(shader);

    GLint result = 0;
    GLchar eLog[1024] = { 0 };

    // retrieve status of the shader and print if any error occured
    glGetShaderiv(shader, GL_COMPILE_STATUS, &result);

    if (result == 0) {
        glGetShaderInfoLog(shader, sizeof(eLog), nullptr, eLog);
        std::cerr << "Error compiling the " << shader_type << " shader:  '" << eLog << "'" << '\n';
        std::cerr << shader_code;
        return;
    }

    // we are happy, everything went well so attach shader to program
    glAttachShader(program, shader);
}

void ShaderProgram::compile_shader_program(const char *vertex_code, const char *fragment_code)
{
    // retrieve the id; we need to reference it later on
    program_id = glCreateProgram();
    program_is_linked = false;

    if (program_id == 0u) {
        std::cerr << "Error creating shader program !" << '\n';
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

void ShaderProgram::compile_shader_program(const char *vertex_code,
  const char *geometry_code,
  const char *fragment_code)
{
    program_id = glCreateProgram();
    program_is_linked = false;

    if (program_id == 0u) {
        std::cerr << "Error creating shader program!" << '\n';
        return;
    }

    add_shader(program_id, vertex_code, GL_VERTEX_SHADER);
    add_shader(program_id, geometry_code, GL_GEOMETRY_SHADER);
    add_shader(program_id, fragment_code, GL_FRAGMENT_SHADER);

    compile_program();
}

void ShaderProgram::compile_compute_shader_program(const char *compute_code)
{
    program_id = glCreateProgram();
    program_is_linked = false;

    if (program_id == 0u) {
        std::cerr << "Error creating shader program!" << '\n';
        return;
    }

    add_shader(program_id, compute_code, GL_COMPUTE_SHADER);

    compile_program();
}

void ShaderProgram::compile_program()
{
    // as simple as that; opengl will link it for us :)
    glLinkProgram(program_id);
    GLint result = 0;
    GLchar eLog[1024] = { 0 };

    glGetProgramiv(program_id, GL_LINK_STATUS, &result);

    if (result == 0) {
        glGetProgramInfoLog(program_id, sizeof(eLog), nullptr, eLog);
        std::cerr << "Error linking program: '" << eLog << "'" << '\n';
        program_is_linked = false;
        return;
    }

    program_is_linked = true;
}

auto ShaderProgram::setUniformVec3(glm::vec3 uniform, const std::string &shaderUniformName) -> bool
{
    bool validity = true;
    GLuint const uniform_location = getUniformLocation(shaderUniformName, validity);

    if (validity) { glUniform3f(uniform_location, uniform.x, uniform.y, uniform.z); }

    return validity;
}

auto ShaderProgram::setUniformFloat(GLfloat uniform, const std::string &shaderUniformName) -> bool
{
    bool validity = true;
    GLuint const uniform_location = getUniformLocation(shaderUniformName, validity);

    if (validity) { glUniform1f(uniform_location, uniform); }

    return validity;
}

auto ShaderProgram::setUniformInt(GLint uniform, const std::string &shaderUniformName) -> bool
{
    bool validity = true;
    GLuint const uniform_location = getUniformLocation(shaderUniformName, validity);

    if (validity) { glUniform1i(uniform_location, uniform); }

    return validity;
}

auto ShaderProgram::setUniformMatrix4fv(glm::mat4 matrix, const std::string &shaderUniformName) -> bool
{
    bool validity = true;
    GLuint const uniform_location = getUniformLocation(shaderUniformName, validity);

    if (validity) { glUniformMatrix4fv(uniform_location, 1, GL_FALSE, glm::value_ptr(matrix)); }

    return validity;
}

auto ShaderProgram::setUniformBlockBinding(GLuint block_binding, const std::string &shaderUniformName) const -> bool
{
    bool validity = true;
    GLuint const uniform_location = glGetUniformBlockIndex(program_id, shaderUniformName.c_str());

    (uniform_location == GL_INVALID_INDEX) ? validity = false : validity = true;

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

auto ShaderProgram::validateUniformLocation(GLint uniformLocation) -> bool
{
    // if uniform location is invalid (f.e. var disappears because of optimizing
    // of unused vars)
    return uniformLocation != -1;
}

auto ShaderProgram::getUniformLocation(const std::string &shaderUniformName, bool &validity) -> GLuint
{
    if (!program_is_linked) {
        validity = false;
        return 0;
    }

    GLint const uniform_location = glGetUniformLocation(program_id, shaderUniformName.c_str());
    validity = validateUniformLocation(uniform_location);
    if (!validity) { return 0; }

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

    return static_cast<GLuint>(uniform_location);
}

void ShaderProgram::clear_shader_program()
{
    // don't trash the id's!!
    // delete it from memory!!
    if (program_id != 0) {
        glDeleteProgram(program_id);
        program_id = 0;
        program_is_linked = false;
    }
}

ShaderProgram::~ShaderProgram() { clear_shader_program(); }
