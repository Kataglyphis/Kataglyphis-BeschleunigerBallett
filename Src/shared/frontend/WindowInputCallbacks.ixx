module;

#include <GLFW/glfw3.h>
#include <imgui.h>

#include "WindowInputState.hpp"

export module kataglyphis.shared.frontend.window_input_callbacks;

export namespace Kataglyphis::Frontend {

using Kataglyphis::Frontend::window_key_count;

inline void reset_window_keys(bool *keys)
{
    for (std::size_t index = 0; index < window_key_count; ++index) { keys[index] = false; }
}

inline float consume_axis_delta(float &axis_change)
{
    float const delta = axis_change;
    axis_change = 0.0F;
    return delta;
}

inline bool imgui_wants_keyboard_capture()
{
    return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
}

inline void handle_key_callback(GLFWwindow *window, bool *keys, int key, int action)
{
    bool const captured = imgui_wants_keyboard_capture();

    // RELEASE must always fall through, even while ImGui holds capture -
    // otherwise a key held when a widget grabs focus is never released and
    // the camera keeps moving forever.
    if (action == GLFW_RELEASE) {
        if (key >= 0 && static_cast<std::size_t>(key) < window_key_count) { keys[key] = false; }
        return;
    }

    if (captured) { return; }

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) { glfwSetWindowShouldClose(window, GLFW_TRUE); }

    if (key >= 0 && static_cast<std::size_t>(key) < window_key_count) {
        if (action == GLFW_PRESS) { keys[key] = true; }
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
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) { return; }

    if (mouse_first_moved) {
        last_x = static_cast<float>(x_pos);
        last_y = static_cast<float>(y_pos);
        mouse_first_moved = false;
    }

    x_change = static_cast<float>(x_pos) - last_x;
    y_change = last_y - static_cast<float>(y_pos);

    last_x = static_cast<float>(x_pos);
    last_y = static_cast<float>(y_pos);
}

inline bool imgui_wants_mouse_capture()
{
    return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
}

inline bool should_capture_cursor(int button, int action)
{
    return (action == GLFW_PRESS) && (button == GLFW_MOUSE_BUTTON_RIGHT);
}

inline void handle_mouse_button_callback(GLFWwindow *window,
  bool &mouse_first_moved,
  int button,
  int action,
  GLFWcursorposfun mouse_callback)
{
    if (should_capture_cursor(button, action)) {
        if (imgui_wants_mouse_capture()) { return; }
        glfwSetCursorPosCallback(window, mouse_callback);
    } else {
        // A release must always end look mode, even while ImGui holds
        // capture - otherwise releasing the mouse over a panel leaves the
        // cursor callback installed forever.
        mouse_first_moved = true;
        glfwSetCursorPosCallback(window, nullptr);
    }
}

}// namespace Kataglyphis::Frontend
