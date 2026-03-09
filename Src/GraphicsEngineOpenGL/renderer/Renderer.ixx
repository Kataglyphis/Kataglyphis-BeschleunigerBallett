module;

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

export module kataglyphis.opengl.renderer;

import kataglyphis.opengl.camera;
import kataglyphis.opengl.gbuffer;
import kataglyphis.opengl.scene;
import kataglyphis.opengl.shader_includes;
import kataglyphis.opengl.render_pass;
import kataglyphis.opengl.omni_shadow_map_pass;
import kataglyphis.opengl.directional_shadow_map_pass;
import kataglyphis.opengl.geometry_pass;
import kataglyphis.opengl.lighting_pass;

export class Renderer
{
  public:
    Renderer(GLuint window_width, GLuint window_height);

    void drawFrame(const std::shared_ptr<Camera> &main_camera,
      const std::shared_ptr<Scene> &scene,
      glm::mat4 projection_matrix,
      GLfloat delta_time);

    void update_window_params(GLuint window_width, GLuint window_height);
    void reload_shader_programs();

    ~Renderer();

  private:
    GLuint window_width, window_height;

    std::shared_ptr<GBuffer> gbuffer;

    ShaderIncludes shader_includes;

    // all render passes
    std::vector<std::shared_ptr<RenderPass>> render_passes;

    std::shared_ptr<OmniShadowMapPass> omni_shadow_map_pass;
    std::shared_ptr<DirectionalShadowMapPass> directional_shadow_map_pass;
    std::shared_ptr<GeometryPass> geometry_pass;
    std::shared_ptr<LightingPass> lighting_pass;
};
