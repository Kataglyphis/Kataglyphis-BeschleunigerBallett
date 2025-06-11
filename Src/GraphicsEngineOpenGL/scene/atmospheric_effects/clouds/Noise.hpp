#pragma once

#include <glad/glad.h>

#include "hostDevice/bindings.hpp"
#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <random>
#include <vector>

#include "compute/ComputeShaderProgram.hpp"

// inspired by:
// http://advances.realtimerendering.com/s2015/The%20Real-time%20Volumetric%20Cloudscapes%20of%20Horizon%20-%20Zero%20Dawn%20-%20ARTR.pdf
class Noise
{
  public:
    Noise();

    void create_res128_noise();
    void create_res32_noise();

    void read_res128_noise();
    void read_res32_noise();

    void update();

    void set_num_cells(GLuint num_cells_per_axis, GLuint index);

    ~Noise();

  private:
    void create_shader_programs();
    void generate_cells(GLuint num_cells_per_axis, GLuint cell_index);

    void generate_textures();
    void generate_num_cells_textures();
    void generate_res128_noise_texture();
    void generate_res32_noise_texture();

    void delete_textures();

    void print_comp_shader_capabilities();

    // first texture dim = 128^3
    GLuint texture_1_id;
    GLuint texture_dim_1;
    std::shared_ptr<ComputeShaderProgram> texture_1_shader_program;

    // 2nd texture dim = 32^3
    GLuint texture_2_id;
    GLuint texture_dim_2;
    std::shared_ptr<ComputeShaderProgram> texture_2_shader_program;

    GLuint cell_ids[NUM_CELL_POSITIONS];
    GLuint num_cells_per_axis[NUM_CELL_POSITIONS];
    std::array<std::vector<GLfloat>, NUM_CELL_POSITIONS> cell_data;
};
