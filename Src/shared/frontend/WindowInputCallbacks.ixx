module;

#include <GLFW/glfw3.h>
#include <imgui.h>

export module kataglyphis.shared.frontend.window_input_callbacks;

export namespace Kataglyphis::Frontend {

constexpr int window_key_count = 1024;

inline void reset_window_keys(bool *keys)
{
    for (int index = 0; index < window_key_count; ++index) { keys[index] = false; }
}

inline float consume_axis_delta(float &axis_change)
{
    float const delta = axis_change;
    axis_change = 0.0F;
    return delta;
}

inline void handle_key_callback(GLFWwindow *window, bool *keys, int key, int action)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) { glfwSetWindowShouldClose(window, GLFW_TRUE); }

    if (key >= 0 && key < window_key_count) {
        if (action == GLFW_PRESS) {
            keys[key] = true;
        } else if (action == GLFW_RELEASE) {
            keys[key] = false;
        }
    }
}

inline void handle_mouse_callback(GLFWwindow *window,
  float &last_x,
  float &last_y,
  float &x_change,
  float &y_change,
  bool &mouse_first_moved,
  double x_pos,
  double y_pos)
{
    (void)window;

    if (mouse_first_moved) {
        last_x = static_cast<float>(x_pos);
        last_y = static_cast<float>(y_pos);
        mouse_first_moved = false;
    }

    x_change = static_cast<float>(x_pos - last_x);
    y_change = static_cast<float>(last_y - y_pos);

    last_x = static_cast<float>(x_pos);
    last_y = static_cast<float>(y_pos);
}

inline bool imgui_wants_mouse_capture(int button, int action)
{
    if (ImGui::GetCurrentContext() == nullptr || !ImGui::GetIO().WantCaptureMouse) { return false; }

    ImGuiIO &io = ImGui::GetIO();
    io.AddMouseButtonEvent(button, action != 0);
    return true;
}

inline void handle_mouse_button_callback(GLFWwindow *window,
  bool &mouse_first_moved,
  int button,
  int action,
  GLFWcursorposfun mouse_callback)
{
    if (imgui_wants_mouse_capture(button, action)) { return; }

    if ((action == GLFW_PRESS) && (button == GLFW_MOUSE_BUTTON_RIGHT)) {
        glfwSetCursorPosCallback(window, mouse_callback);
    } else {
        mouse_first_moved = true;
        glfwSetCursorPosCallback(window, nullptr);
    }
}

}// namespace Kataglyphis::Frontend
