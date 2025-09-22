#pragma once
#include <memory>

#include "Noise.hpp"
#include "renderer/ShaderProgram.hpp"
#include "scene/AABB.hpp"
#include "util/RandomNumbers.hpp"

class Clouds
{
  public:
    Clouds();

    void render(glm::mat4 projection_matrix, glm::mat4 view_matrix, GLuint window_width, GLuint window_height);

    void read();

    void create_noise_textures();

    glm::mat4 get_model() const;
    glm::vec3 get_movement_direction() const { return movement_direction; };
    glm::vec3 get_radius() const { return scale_factor / 2.f; };
    glm::mat4 get_normal_model() const { return glm::transpose(glm::inverse(model)); };
    glm::vec3 get_mesh_scale() const { return scale_factor; };
    GLfloat get_movement_speed() const { return movement_speed; };
    GLfloat get_density() const { return density; };
    GLfloat get_scale() const { return scale; };
    GLfloat get_pillowness() const { return pillowness; };
    GLfloat get_cirrus_effect() const { return cirrus_effect; };
    GLuint get_num_march_steps() const { return num_march_steps; };
    GLuint get_num_march_steps_to_light() const { return num_march_steps_to_light; };
    bool get_powder_effect() const { return powder_effect; };

    std::shared_ptr<ShaderProgram> get_shader_program() const { return shader_program; };

    void set_powder_effect(bool cloud_powder_effect);
    void set_cirrus_effect(GLfloat cirrus_effect);
    void set_pillowness(GLfloat cloud_pillowness);
    void set_scale(GLfloat scale);
    void set_density(GLfloat density);
    void set_movement_speed(GLfloat speed);
    void set_scale(glm::vec3 scale);
    void set_translation(glm::vec3 translation);
    void set_movement_direction(glm::vec3 movement_dir);
    void set_num_march_steps(GLuint num_march_steps);
    void set_num_march_steps_to_light(GLuint num_march_steps_to_light);

    ~Clouds();

  private:
    std::shared_ptr<ShaderProgram> shader_program;
    std::shared_ptr<Noise> noise;
    std::shared_ptr<RandomNumbers> random_numbers;

    glm::mat4 model;
    AABB aabb;

    GLfloat minX, maxX, minY, maxY, minZ, maxZ;

    glm::vec3 movement_direction;
    glm::vec3 scale_factor, translation;

    GLfloat movement_speed, density, scale, pillowness, cirrus_effect;

    GLuint num_march_steps, num_march_steps_to_light;

    bool powder_effect;
};
