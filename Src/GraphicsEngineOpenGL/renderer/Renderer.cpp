module;

#include <glad/glad.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <cstddef>
#include <memory>
#include <vector>

module kataglyphis.opengl.renderer;

import kataglyphis.opengl.camera;
import kataglyphis.opengl.gbuffer;
import kataglyphis.opengl.scene;
import kataglyphis.opengl.render_pass;
import kataglyphis.opengl.omni_shadow_map_pass;
import kataglyphis.opengl.directional_shadow_map_pass;
import kataglyphis.opengl.geometry_pass;
import kataglyphis.opengl.lighting_pass;
import kataglyphis.opengl.point_light;

Renderer::Renderer(GLuint window_width, GLuint window_height)
  :

    window_width(window_width), window_height(window_height),
    gbuffer(std::make_shared<GBuffer>(window_width, window_height)),
    omni_shadow_map_pass(std::make_shared<OmniShadowMapPass>()),
    directional_shadow_map_pass(std::make_shared<DirectionalShadowMapPass>()),
    geometry_pass(std::make_shared<GeometryPass>()), lighting_pass(std::make_shared<LightingPass>())

{
    render_passes.push_back(omni_shadow_map_pass);
    render_passes.push_back(directional_shadow_map_pass);
    render_passes.push_back(geometry_pass);
    render_passes.push_back(lighting_pass);

    gbuffer->create();
}

void Renderer::drawFrame(const std::shared_ptr<Camera> &main_camera,
  const std::shared_ptr<Scene> &scene,
  glm::mat4 projection_matrix,
  GLfloat delta_time)
{
    directional_shadow_map_pass->execute(projection_matrix, main_camera, window_width, window_height, scene);

    // omni shadow map passes for our point lights
    std::vector<std::shared_ptr<PointLight>> p_lights = scene->get_point_lights();
    for (size_t p_light_count = 0; p_light_count < scene->get_point_light_count(); p_light_count++) {
        omni_shadow_map_pass->execute(p_lights[p_light_count], scene);
    }

    // we will now start the geometry pass
    geometry_pass->execute(
      projection_matrix, main_camera, window_width, window_height, gbuffer->get_id(), delta_time, scene);

    // after geometry pass we can now do the lighting
    lighting_pass->execute(projection_matrix, main_camera, scene, gbuffer, delta_time);
}

void Renderer::update_window_params(GLuint width, GLuint height)
{
    window_width = width;
    window_height = height;
    gbuffer->update_window_params(width, height);
    gbuffer->create();
}

void Renderer::reload_shader_programs()
{
    // also reload all shader include files
    shader_includes = ShaderIncludes();

    for (const std::shared_ptr<RenderPass> &render_pass : render_passes) { render_pass->create_shader_program(); }
}

Renderer::~Renderer() = default;
