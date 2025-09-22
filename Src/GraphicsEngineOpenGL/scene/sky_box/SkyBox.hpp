#pragma once
#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>

#include "hostDevice/GlobalValues.hpp"
#include "renderer/ShaderProgram.hpp"
#include "scene/Mesh.hpp"
class SkyBox
{
  public:
    SkyBox();

    void draw_sky_box(glm::mat4 projection_matrix,
      glm::mat4 view_matrix,
      GLuint window_width,
      GLuint window_height,
      GLfloat delta_time);

    void reload();

    ~SkyBox();

  private:
    GLfloat movement_speed = 0.1f;

    GLfloat shader_playback_time;

    std::shared_ptr<Mesh> sky_mesh;
    std::shared_ptr<ShaderProgram> shader_program;

    GLuint texture_id;
    GLuint uniform_projection, uniform_view;
};
