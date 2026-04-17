module;

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

export module kataglyphis.vulkan.light;

export namespace Kataglyphis {
class Light
{
  public:
    Light() : color(1.0f), radiance(1.0f) {}

    Light(float red, float green, float blue, float rad) : color(red, green, blue), radiance(rad) {}

    glm::vec3 get_color() const { return color; }
    float get_radiance() const { return radiance; }

    virtual ~Light() = default;

  protected:
    glm::vec3 color;
    float radiance;
    glm::mat4 light_proj{};
};

class DirectionalLight : public Light
{
  public:
    DirectionalLight() : Light(), direction(0.0f, -1.0f, 0.0f) {}

    DirectionalLight(float red, float green, float blue, float rad, float x_dir, float y_dir, float z_dir)
      : Light(red, green, blue, rad), direction(x_dir, y_dir, z_dir)
    {}

    glm::vec3 get_direction() const { return direction; }
    void set_direction(glm::vec3 dir) { direction = dir; }

    // Placeholder for cascaded shadow logic
    // We will implement `calc_orthogonal_projections` and `update_shadow_map` later

  private:
    glm::vec3 direction;
};

class PointLight : public Light
{
  public:
    PointLight() : Light(), position(0.0f), constant(1.0f), linear(0.09f), exponent(0.032f), far_plane(100.0f) {}

    PointLight(float red, float green, float blue, float rad, float x_pos, float y_pos, float z_pos, float con, float lin, float exp, float f_plane)
      : Light(red, green, blue, rad), position(x_pos, y_pos, z_pos), constant(con), linear(lin), exponent(exp), far_plane(f_plane)
    {}

    glm::vec3 get_position() const { return position; }
    void set_position(glm::vec3 pos) { position = pos; }

    float get_constant_factor() const { return constant; }
    float get_linear_factor() const { return linear; }
    float get_exponent_factor() const { return exponent; }
    float get_far_plane() const { return far_plane; }

  private:
    glm::vec3 position;
    float constant, linear, exponent;
    float far_plane;
};

}