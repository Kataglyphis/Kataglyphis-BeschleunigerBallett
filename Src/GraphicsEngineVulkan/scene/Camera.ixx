module;

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

export module kataglyphis.vulkan.camera;

export class Camera
{
  public:
    Camera();

    void key_control(const bool *keys, float delta_time);
    void mouse_control(float x_change, float y_change);

    glm::vec3 get_camera_position() const { return position; };
    glm::vec3 get_camera_direction() const { return glm::normalize(front); };
    glm::vec3 get_up_axis() const { return up; };
    glm::vec3 get_right_axis() const { return right; };
    float get_near_plane() const { return near_plane; };
    float get_far_plane() const { return far_plane; };
    float get_fov() const { return fov; };
    float get_yaw() const { return yaw; };

    glm::mat4 calculate_viewmatrix();

    void set_near_plane(float near_plane);
    void set_far_plane(float far_plane);
    void set_fov(float fov);
    void set_camera_position(glm::vec3 new_camera_position);

    ~Camera();

  private:
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 world_up;
    glm::vec3 right;
    glm::vec3 up;

    float yaw;
    float pitch;

    float movement_speed;
    float turn_speed;

    float near_plane, far_plane, fov;

    void update();
};
