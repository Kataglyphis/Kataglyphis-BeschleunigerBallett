module;

#include <memory>
#include <utility>
#include <vector>
#include <algorithm>
#include <limits>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

#include "hostDevice/host_device_shared.hpp"

module kataglyphis.opengl.directional_light;

import kataglyphis.opengl.directional_light.cascaded_shadow_map;

DirectionalLight::DirectionalLight()
  :

    shadow_map(std::make_shared<CascadedShadowMap>()),

    direction(glm::vec3{ 0, 0, 0 }),

    shadow_near_plane(0.F), shadow_far_plane(0.F),

    cascade_light_matrices(NUM_CASCADES, glm::mat4(0.F))

{
    light_proj = glm::ortho(-20.0F, 20.0F, -20.0F, 20.0F, 0.1F, 100.F);
}

DirectionalLight::DirectionalLight(GLuint shadow_width,
  GLuint shadow_height,
  GLfloat red,
  GLfloat green,
  GLfloat blue,
  GLfloat radiance,
  GLfloat x_dir,
  GLfloat y_dir,
  GLfloat z_dir,
  GLfloat near_plane,
  GLfloat far_plane,
  int num_cascades)
  :

    Light(red, green, blue, radiance), shadow_map(std::make_shared<CascadedShadowMap>()),
    direction(glm::vec3{ x_dir, y_dir, z_dir }),

    shadow_near_plane(near_plane), shadow_far_plane(far_plane),

    cascade_light_matrices(NUM_CASCADES, glm::mat4(0.F))

{
    light_proj = glm::ortho(-20.0F, 20.0F, -20.0F, 20.0F, 0.1F, 100.F);

    shadow_map->init(shadow_width, shadow_height, static_cast<GLuint>(num_cascades));
}

auto DirectionalLight::get_light_view_matrix() const -> glm::mat4
{
    return glm::lookAt(direction, glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(0.0F, 1.0F, 0.0F));
}

auto DirectionalLight::get_direction() const -> glm::vec3 { return direction; }

auto DirectionalLight::get_color() const -> glm::vec3 { return color; }

auto DirectionalLight::get_radiance() const -> float { return radiance; }

auto DirectionalLight::get_cascaded_slots() const -> std::vector<GLfloat>
{
    std::vector<GLfloat> result;

    result.reserve(NUM_CASCADES + 1);
    for (size_t i = 0; i < NUM_CASCADES + 1; i++) { result.push_back(cascade_slots[i]); }

    return result;
}

auto DirectionalLight::get_cascaded_light_matrices() -> std::vector<glm::mat4> & { return cascade_light_matrices; }

void DirectionalLight::update_shadow_map(GLfloat shadow_width, GLfloat shadow_height, GLuint num_cascades)
{
    shadow_map = std::make_shared<CascadedShadowMap>();
    shadow_map->init(static_cast<GLuint>(shadow_width), static_cast<GLuint>(shadow_height), num_cascades);
}

auto DirectionalLight::get_frustum_corners_world_space(const glm::mat4 &proj, const glm::mat4 &view)
  -> std::vector<glm::vec4>
{
    const auto inv = glm::inverse(proj * view);

    std::vector<glm::vec4> frustumCorners;
    for (float x = 0; x < 2; ++x) {
        for (float y = 0; y < 2; ++y) {
            for (float z = 0; z < 2; ++z) {
                const glm::vec4 pt = inv * glm::vec4((2.0F * x) - 1.0F, (2.0F * y) - 1.0F, (2.0F * z) - 1.0F, 1.0F);
                frustumCorners.push_back(pt / pt.w);
            }
        }
    }

    return frustumCorners;
}

void DirectionalLight::calc_cascaded_slots()
{
    GLuint const number_of_elements = shadow_map->get_num_active_cascades();

    for (size_t i = 0; i < NUM_CASCADES + 1; i++) { cascade_slots[i] = 100000.F; }

    for (size_t i = 0; std::cmp_less(i, number_of_elements + 1); i++) {
        if (i == 0) {
            (cascade_slots)[i] = shadow_near_plane;

        } else {
            (cascade_slots)[i] =
              (shadow_far_plane) * (static_cast<GLfloat>(i) / static_cast<GLfloat>(number_of_elements));
        }
    }

    // cascade_slots = { shadow_near_plane, shadow_far_plane / 50.f,
    // shadow_far_plane / 25.f, shadow_far_plane };
}

void DirectionalLight::calc_orthogonal_projections(glm::mat4 camera_view_matrix,
  GLfloat fov,
  GLuint window_width,
  GLuint window_height,
  GLuint current_num_cascades)
{
    // calc the start and end point for our cascaded shadow maps
    calc_cascaded_slots();

    for (size_t i = 0; std::cmp_less(i, current_num_cascades); i++) {
        glm::mat4 const curr_cascade_proj = glm::perspective(glm::radians(fov),
          static_cast<float>(window_width) / static_cast<float>(window_height),
          cascade_slots[i],
          cascade_slots[i + 1]);

        std::vector<glm::vec4> frustumCornerWorldSpace =
          get_frustum_corners_world_space(curr_cascade_proj, camera_view_matrix);

        glm::vec3 center = glm::vec3(0, 0, 0);
        for (const auto &v : frustumCornerWorldSpace) { center += glm::vec3(v); }

        center /= frustumCornerWorldSpace.size();

        glm::mat4 const light_view_matrix = glm::lookAt(center - get_direction(), center, glm::vec3(0.0F, 1.0F, 0.0F));

        // the # of frustum corners = 8
        GLfloat minX = std::numeric_limits<float>::max();
        GLfloat maxX = std::numeric_limits<float>::min();
        GLfloat minY = std::numeric_limits<float>::max();
        GLfloat maxY = std::numeric_limits<float>::min();
        GLfloat minZ = std::numeric_limits<float>::max();
        GLfloat maxZ = std::numeric_limits<float>::min();

        for (auto m : frustumCornerWorldSpace) {
            // transform each corner from view to world space
            glm::vec4 const v_light_view = light_view_matrix * m;
            // now go to light space
            minX = std::min(minX, v_light_view.x);
            maxX = std::max(maxX, v_light_view.x);
            // we always have negativ y values
            minY = std::min(minY, v_light_view.y);
            maxY = std::max(maxY, v_light_view.y);

            minZ = std::min(minZ, v_light_view.z);
            maxZ = std::max(maxZ, v_light_view.z);
        }

        // Tune this parameter according to the scene
        // for having objects casting shadows that are actually not in the frustum
        // :)
        constexpr float zMult = 10.0F;
        if (minZ < 0) {
            minZ *= zMult;
        } else {
            minZ /= zMult;
        }
        if (maxZ < 0) {
            maxZ /= zMult;
        } else {
            maxZ *= zMult;
        }

        glm::mat4 const light_projection = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);

        cascade_light_matrices[i] = light_projection * light_view_matrix;
    }
}

auto DirectionalLight::calculate_light_transform() -> glm::mat4 { return light_proj * get_light_view_matrix(); }

void DirectionalLight::set_direction(glm::vec3 dir) { this->direction = dir; }

void DirectionalLight::set_radiance(float rad) { this->radiance = rad; }

void DirectionalLight::set_color(glm::vec3 col) { this->color = col; }

DirectionalLight::~DirectionalLight() = default;
