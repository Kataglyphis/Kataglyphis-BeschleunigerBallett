#include "Noise.hpp"

#include <sstream>

Noise::Noise()
  :

    texture_dim_1(128), texture_dim_2(32)

{
    create_shader_programs();

    // we need 3d-voxel grids with different sizes for
    // creating different worley frequencies
    for (int i = 0; i < NUM_CELL_POSITIONS; i++) {
        num_cells_per_axis[i] = static_cast<GLuint>(pow(2, i + 1));
        generate_cells(num_cells_per_axis[i], i);
    }

    generate_textures();
}

void Noise::create_shader_programs()
{
    texture_1_shader_program = std::make_shared<ComputeShaderProgram>();
    texture_2_shader_program = std::make_shared<ComputeShaderProgram>();

    texture_1_shader_program->create_computer_shader_program_from_file("clouds/noise_texture_128_res.comp");
    texture_2_shader_program->create_computer_shader_program_from_file("clouds/noise_texture_32_res.comp");
}

void Noise::generate_textures()
{
    generate_num_cells_textures();
    generate_res128_noise_texture();
    generate_res32_noise_texture();
}

void Noise::generate_num_cells_textures()
{
    glGenTextures(NUM_CELL_POSITIONS, cell_ids);

    for (int i = 0; i < NUM_CELL_POSITIONS; i++) {
        glBindTexture(GL_TEXTURE_3D, cell_ids[i]);

        glTexImage3D(GL_TEXTURE_3D,
          0,
          GL_RGBA32F,
          num_cells_per_axis[i],
          num_cells_per_axis[i],
          num_cells_per_axis[i],
          0,
          GL_RGBA,
          GL_FLOAT,
          cell_data[i].data());

        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        glBindTexture(GL_TEXTURE_3D, 0);
    }
}

void Noise::generate_res128_noise_texture()
{
    glGenTextures(1, &texture_1_id);

    glActiveTexture(GL_TEXTURE0 + NOISE_128D_TEXTURES_SLOT);
    glBindTexture(GL_TEXTURE_3D, texture_1_id);

    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA32F, texture_dim_1, texture_dim_1, texture_dim_1, 0, GL_RGBA, GL_FLOAT, NULL);

    glBindImageTexture(NOISE_128D_IMAGE_SLOT, texture_1_id, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);

    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_3D, 0);
}

void Noise::generate_res32_noise_texture()
{
    glGenTextures(1, &texture_2_id);

    glActiveTexture(GL_TEXTURE0 + NOISE_32D_TEXTURES_SLOT);
    glBindTexture(GL_TEXTURE_3D, texture_2_id);

    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA32F, texture_dim_2, texture_dim_2, texture_dim_2, 0, GL_RGBA, GL_FLOAT, NULL);

    glBindImageTexture(NOISE_32D_IMAGE_SLOT, texture_2_id, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);

    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_3D, 0);
}

void Noise::print_comp_shader_capabilities()
{
    int work_grp_cnt[3];

    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &work_grp_cnt[0]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &work_grp_cnt[1]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &work_grp_cnt[2]);

    printf("max global (total) work group counts x:%i y:%i z:%i\n", work_grp_cnt[0], work_grp_cnt[1], work_grp_cnt[2]);

    int work_grp_size[3];

    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &work_grp_size[0]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &work_grp_size[1]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &work_grp_size[2]);

    printf("max local (in one shader) work group sizes x:%i y:%i z:%i\n",
      work_grp_size[0],
      work_grp_size[1],
      work_grp_size[2]);
}

void Noise::update()
{
    delete_textures();

    texture_1_shader_program->reload();
    texture_2_shader_program->reload();

    for (int i = 0; i < NUM_CELL_POSITIONS; i++) { generate_cells(num_cells_per_axis[i], i); }

    generate_textures();

    create_res128_noise();
    create_res32_noise();
}

void Noise::set_num_cells(GLuint num_cells_per_axis, GLuint index)
{
    this->num_cells_per_axis[index] = num_cells_per_axis;
}
/**

        @ num_cells_per_axis:	current voxel grid dimension
        @ cell_index:			index into global array for all voxel
   grids

        needed for generating different worley frequencies later on

*/
void Noise::generate_cells(GLuint num_cells_per_axis, GLuint cell_index)
{
    cell_data[cell_index].reserve(num_cells_per_axis * num_cells_per_axis * num_cells_per_axis * 4);

    // guess which birthday this is ;)
    std::mt19937_64 gen64(25121995);
    std::uniform_real_distribution<float> dis(0, 1);

    // depth
    for (int i = 0; i < static_cast<int>(num_cells_per_axis); i++) {
        // height
        for (int k = 0; k < static_cast<int>(num_cells_per_axis); k++) {
            // width
            for (int m = 0; m < static_cast<int>(num_cells_per_axis); m++) {
                // from:
                // https://www.khronos.org/registry/OpenGL-Refpages/gl4/html/glTexImage3D.xhtml
                // "The first element corresponds to the lower left corner of the
                // texture image. Subsequent elements progress left-to-right through the
                // remaining texels in the lowest row of the texture image, and then in
                // successively higher rows of the texture image. The final element
                // corresponds to the upper right corner of the texture image."

                const GLfloat random_offset[3] = { dis(gen64), dis(gen64), dis(gen64) };

                GLfloat position[3] = { (m + random_offset[0]), (k + random_offset[1]), (i + random_offset[2]) };

                cell_data[cell_index].push_back(position[0]);
                cell_data[cell_index].push_back(position[1]);
                cell_data[cell_index].push_back(position[2]);
                cell_data[cell_index].push_back(1.0f);

                // i leave this more c-style approach for my further me
                // to clearify things :)
                // GLuint index = (i + num_cells_per_axis * (k + m *
                // num_cells_per_axis)) * 4;
                /*GLfloat position[3] = {	(i + random_offset[0]),
                                                                                        (k + random_offset[1]),
                                                                                        (m + random_offset[2])};*/

                /*cell_data[cell_index][index]		= position[0];
                                        cell_data[cell_index][index + 1]	=
                   position[1]; cell_data[cell_index][index + 2]	= position[2];
                                        cell_data[cell_index][index + 3]
                   = 1.0f;*/
            }
        }
    }
}

void Noise::create_res128_noise()
{
    texture_1_shader_program->use_shader_program();

    texture_1_shader_program->setUniformInt(NOISE_128D_IMAGE_SLOT, "noise");

    std::stringstream ss;

    for (uint32_t i = 0; i < NUM_CELL_POSITIONS; i++) {
        ss << "cell_positions[" << i << "]";
        texture_1_shader_program->setUniformInt(NOISE_CELL_POSITIONS_SLOT + i, ss.str());
        ss.clear();
        ss.str(std::string());

        ss << "num_cells[" << i << "]";
        texture_1_shader_program->setUniformInt(num_cells_per_axis[i], ss.str());
        ss.clear();
        ss.str(std::string());

        glActiveTexture(GL_TEXTURE0 + NOISE_CELL_POSITIONS_SLOT + i);
        glBindTexture(GL_TEXTURE_3D, cell_ids[i]);
    }

    glDispatchCompute((GLuint)texture_dim_1, (GLuint)texture_dim_1, (GLuint)texture_dim_1);

    // make sure writing to image has finished before read
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    // glMemoryBarrier(GL_ALL_BARRIER_BITS);

    glBindTexture(GL_TEXTURE_3D, 0);
}

void Noise::create_res32_noise()
{
    texture_2_shader_program->use_shader_program();

    texture_2_shader_program->setUniformInt(NOISE_32D_IMAGE_SLOT, "noise");

    std::stringstream ss;

    for (uint32_t i = 0; i < NUM_CELL_POSITIONS; i++) {
        ss << "cell_positions[" << i << "]";
        texture_2_shader_program->setUniformInt(NOISE_CELL_POSITIONS_SLOT + i, ss.str());
        ss.clear();
        ss.str(std::string());

        ss << "num_cells[" << i << "]";
        texture_2_shader_program->setUniformInt(num_cells_per_axis[i], ss.str());
        ss.clear();
        ss.str(std::string());

        glActiveTexture(GL_TEXTURE0 + NOISE_CELL_POSITIONS_SLOT + i);
        glBindTexture(GL_TEXTURE_3D, cell_ids[i]);
    }

    glDispatchCompute((GLuint)texture_dim_2, (GLuint)texture_dim_2, (GLuint)texture_dim_2);

    // make sure writing to image has finished before read
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    // glMemoryBarrier(GL_ALL_BARRIER_BITS);
    glBindTexture(GL_TEXTURE_3D, 0);
}

void Noise::read_res128_noise()
{
    GLuint texture_index = GL_TEXTURE0 + NOISE_128D_TEXTURES_SLOT;
    glActiveTexture((GLenum)texture_index);
    glBindTexture(GL_TEXTURE_3D, texture_1_id);
}

void Noise::read_res32_noise()
{
    GLuint texture_index = GL_TEXTURE0 + NOISE_32D_TEXTURES_SLOT;
    glActiveTexture((GLenum)texture_index);
    glBindTexture(GL_TEXTURE_3D, texture_2_id);
}

void Noise::delete_textures()
{
    glDeleteTextures(1, &texture_1_id);
    glDeleteTextures(1, &texture_2_id);

    glDeleteTextures(NUM_CELL_POSITIONS, cell_ids);
}

Noise::~Noise() { delete_textures(); }
