module;

#include <GLFW/glfw3.h>

export module kataglyphis.shared.frontend.frame_input;

export namespace Kataglyphis::Frontend {

inline void update_frame_timing(float &delta_time, float &last_time)
{
    float const now = static_cast<float>(glfwGetTime());
    delta_time = now - last_time;
    last_time = now;
}

template <typename WindowType, typename CameraType>
inline void process_camera_input(WindowType *window, CameraType *camera, float delta_time)
{
    camera->key_control(window->get_keys(), delta_time);
    camera->mouse_control(window->get_x_change(), window->get_y_change());
}

}// namespace Kataglyphis::Frontend
