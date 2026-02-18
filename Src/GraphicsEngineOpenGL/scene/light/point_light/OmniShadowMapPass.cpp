#include "scene/light/point_light/OmniShadowMapPass.hpp"
#include "scene/light/point_light/PointLight.hpp"
#include "scene/Scene.hpp"
#include "scene/GameObject.hpp"

#include <memory>
#include <glad/glad.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <cstdint>
#include <sstream>
#include <vector>

OmniShadowMapPass::OmniShadowMapPass() { create_shader_program(); }

void OmniShadowMapPass::execute(const std::shared_ptr<PointLight> &p_light, const std::shared_ptr<Scene> &scene)
{
    shader_program->use_shader_program();

    glViewport(
      0, 0, p_light->get_omni_shadow_map()->get_shadow_width(), p_light->get_omni_shadow_map()->get_shadow_height());

    p_light->get_omni_shadow_map()->write();
    glClear(GL_DEPTH_BUFFER_BIT);

    shader_program->setUniformVec3(p_light->get_position(), "light_pos");
    shader_program->setUniformFloat(p_light->get_far_plane(), "far_plane");

    std::vector<glm::mat4> light_matrices = p_light->calculate_light_transform();

    std::stringstream ss;
    for (uint32_t i = 0; i < 6; i++) {
        ss << "light_matrices[" << i << "]";
        shader_program->setUniformMatrix4fv(light_matrices[i], ss.str());
    }

    shader_program->validate_program();

    std::vector<std::shared_ptr<GameObject>> const game_objects = scene->get_game_objects();

    for (const std::shared_ptr<GameObject> &object : game_objects) {
        /* if (object_is_visible(object)) {*/
        set_game_object_uniforms(object->get_world_trafo(), object->get_normal_world_trafo());

        object->render();
        //}
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OmniShadowMapPass::set_game_object_uniforms(glm::mat4 model, glm::mat4 /*normal_model*/)
{
    shader_program->setUniformMatrix4fv(model, "model");
}

void OmniShadowMapPass::create_shader_program()
{
    shader_program = std::make_shared<OmniDirShadowShaderProgram>(OmniDirShadowShaderProgram{});
    shader_program->create_from_files("rasterizer/shadows/omni_shadow_map.vert",
      "rasterizer/shadows/omni_shadow_map.geom",
      "rasterizer/shadows/omni_shadow_map.frag");
}

OmniShadowMapPass::~OmniShadowMapPass() = default;
