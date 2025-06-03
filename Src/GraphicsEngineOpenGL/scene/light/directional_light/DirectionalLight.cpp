#include "scene/light/directional_light/DirectionalLight.hpp"

DirectionalLight::DirectionalLight()
  :

    Light(), shadow_map(std::make_shared<CascadedShadowMap>()),

    direction(glm::vec3{ 0, 0, 0 }),

    shadow_near_plane(0.f), shadow_far_plane(0.f),

    cascade_light_matrices(NUM_CASCADES, glm::mat4(0.f))

{
    light_proj = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 0.1f, 100.f);
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

    cascade_light_matrices(NUM_CASCADES, glm::mat4(0.f))

{
    light_proj = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 0.1f, 100.f);

    shadow_map->init(shadow_width, shadow_height, num_cascades);
}

glm::mat4 DirectionalLight::get_light_view_matrix() const
{
    return glm::lookAt(direction, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::vec3 DirectionalLight::get_direction() const { return direction; }

glm::vec3 DirectionalLight::get_color() const { return color; }

float DirectionalLight::get_radiance() const { return radiance; }

std::vector<GLfloat> DirectionalLight::get_cascaded_slots() const
{
    std::vector<GLfloat> result;

    for (int i = 0; i < NUM_CASCADES + 1; i++) { result.push_back(cascade_slots[i]); }

    return result;
}

std::vector<glm::mat4> &DirectionalLight::get_cascaded_light_matrices() { return cascade_light_matrices; }

void DirectionalLight::update_shadow_map(GLfloat shadow_width, GLfloat shadow_height, GLuint num_cascades)
{
    shadow_map.reset(new CascadedShadowMap);
    shadow_map->init((GLuint)shadow_width, (GLuint)shadow_height, num_cascades);
}

std::vector<glm::vec4> DirectionalLight::get_frustum_corners_world_space(const glm::mat4 &proj, const glm::mat4 &view)
{
    const auto inv = glm::inverse(proj * view);

    std::vector<glm::vec4> frustumCorners;
    for (unsigned int x = 0; x < 2; ++x) {
        for (unsigned int y = 0; y < 2; ++y) {
            for (unsigned int z = 0; z < 2; ++z) {
                const glm::vec4 pt = inv * glm::vec4(2.0f * x - 1.0f, 2.0f * y - 1.0f, 2.0f * z - 1.0f, 1.0f);
                frustumCorners.push_back(pt / pt.w);
            }
        }
    }

    return frustumCorners;
}

void DirectionalLight::calc_cascaded_slots()
{
    GLuint number_of_elements = shadow_map->get_num_active_cascades();

    for (int i = 0; i < NUM_CASCADES + 1; i++) { cascade_slots[i] = 100000.f; }

    for (int i = 0; i < static_cast<int>(number_of_elements + 1); i++) {
        if (i == 0) {
            (cascade_slots)[i] = shadow_near_plane;

        } else {
            (cascade_slots)[i] = (shadow_far_plane) * ((GLfloat)i / (GLfloat)(number_of_elements));
        }
    }

    // cascade_slots = { shadow_near_plane, shadow_far_plane / 50.f,
    // shadow_far_plane / 25.f, shadow_far_plane };

    return;
}

void DirectionalLight::calc_orthogonal_projections(glm::mat4 camera_view_matrix,
  GLfloat fov,
  GLuint window_width,
  GLuint window_height,
  GLuint current_num_cascades)
{
    // calc the start and end point for our cascaded shadow maps
    calc_cascaded_slots();

    for (int i = 0; i < static_cast<int>(current_num_cascades); i++) {
        glm::mat4 curr_cascade_proj = glm::perspective(
          glm::radians(fov), (float)window_width / (float)window_height, cascade_slots[i], cascade_slots[i + 1]);

        std::vector<glm::vec4> frustumCornerWorldSpace =
          get_frustum_corners_world_space(curr_cascade_proj, camera_view_matrix);

        glm::vec3 center = glm::vec3(0, 0, 0);
        for (const auto &v : frustumCornerWorldSpace) { center += glm::vec3(v); }

        center /= frustumCornerWorldSpace.size();

        glm::mat4 light_view_matrix = glm::lookAt(center - get_direction(), center, glm::vec3(0.0f, 1.0f, 0.0f));

        // the # of frustum corners = 8
        GLfloat minX = std::numeric_limits<float>::max();
        GLfloat maxX = std::numeric_limits<float>::min();
        GLfloat minY = std::numeric_limits<float>::max();
        GLfloat maxY = std::numeric_limits<float>::min();
        GLfloat minZ = std::numeric_limits<float>::max();
        GLfloat maxZ = std::numeric_limits<float>::min();

        for (unsigned int m = 0; m < frustumCornerWorldSpace.size(); m++) {
            // transform each corner from view to world space
            glm::vec4 v_light_view = light_view_matrix * frustumCornerWorldSpace[m];
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
        constexpr float zMult = 10.0f;
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

        glm::mat4 light_projection = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);

        cascade_light_matrices[i] = light_projection * light_view_matrix;
    }
}

glm::mat4 DirectionalLight::calculate_light_transform() { return light_proj * get_light_view_matrix(); }

void DirectionalLight::set_direction(glm::vec3 direction) { this->direction = direction; }

void DirectionalLight::set_radiance(float radiance) { this->radiance = radiance; }

void DirectionalLight::set_color(glm::vec3 color) { this->color = color; }

DirectionalLight::~DirectionalLight() {}
