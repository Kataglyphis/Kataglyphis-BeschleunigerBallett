module;
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "hostDevice/GlobalValues.hpp"
#include "hostDevice/host_device_shared.hpp"

export module kataglyphis.opengl.directional_light.cascaded_shadow_map;

export class CascadedShadowMap
{
  public:
    CascadedShadowMap();

    bool init(GLuint width, GLuint height, GLuint num_cascades);
    void write() const;
    void read(GLenum texture_unit) const;

    void write_light_matrices(std::vector<glm::mat4x4> &lightMatrices) const;
    void set_pcf_radius(GLuint radius);
    void set_intensity(GLfloat intensity);

    GLfloat get_intensity() const { return intensity; };
    GLuint get_shadow_width() const { return shadow_width; };
    GLuint get_shadow_height() const { return shadow_height; }
    GLuint get_id() const { return shadow_maps; };
    GLuint get_num_active_cascades() const { return num_active_cascades; };
    GLuint get_pcf_radius() const { return pcf_radius; };

    ~CascadedShadowMap();

  protected:
    GLuint FBO, shadow_maps;

    GLuint shadow_width, shadow_height;

    GLuint matrices_UBO;

    GLuint num_active_cascades;

    GLuint pcf_radius;

    GLfloat intensity;
};
