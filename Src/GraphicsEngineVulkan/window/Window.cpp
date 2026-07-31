module;
#include "GLFW/glfw3.h"
#include "spdlog/spdlog.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <iostream>

#include <vulkan/vulkan.hpp>

module kataglyphis.vulkan.window;

import kataglyphis.shared.frontend.window_input_callbacks;

using namespace Kataglyphis::Frontend;
// GLFW Callback functions
static void onErrorCallback(int error, const char *description)
{
    std::cerr << "GLFW Error " << error << ": " << description << '\n';
}

Window::Window()
  :

    window_width(800.F), window_height(600.F), framebuffer_resized(false)

{
    Kataglyphis::Frontend::reset_window_keys(input_state.keys.data());

    initialize();
}

// please use this constructor; never the standard
Window::Window(uint32_t window_width, uint32_t window_height)
  :

    window_width(window_width), window_height(window_height), framebuffer_resized(false)

{
    Kataglyphis::Frontend::reset_window_keys(input_state.keys.data());

    initialize();
}

auto Window::initialize() -> int
{
    glfwSetErrorCallback(onErrorCallback);
    if (glfwInit() == 0) {
        std::cerr << "GLFW Init failed!" << '\n';
        glfwTerminate();
        return 1;
    }

    if (glfwVulkanSupported() == 0) { spdlog::error("No Vulkan Supported!"); }

    // allow it to resize
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // retrieve new window
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    main_window = glfwCreateWindow(static_cast<int>(window_width),
      static_cast<int>(window_height),
      "\\__/ Epic graphics from hell \\__/ ",
      nullptr,
      nullptr);

    if (main_window == nullptr) {
        std::cerr << "GLFW Window creation failed!" << '\n';
        glfwTerminate();
        return 1;
    }

    init_callbacks();

    return 0;
}

void Window::cleanUp()
{
    glfwDestroyWindow(main_window);
    glfwTerminate();
}

auto Window::get_x_change() -> float { return Kataglyphis::Frontend::consume_axis_delta(input_state.x_change); }

auto Window::get_y_change() -> float { return Kataglyphis::Frontend::consume_axis_delta(input_state.y_change); }

auto Window::framebuffer_size_has_changed() const -> bool { return framebuffer_resized; }

void Window::init_callbacks()
{
    // TODO(jsh): remember this section for our later game logic
    // for the space ship to fly around
    glfwSetWindowUserPointer(main_window, this);
    glfwSetKeyCallback(main_window, &key_callback);
    glfwSetMouseButtonCallback(main_window, &mouse_button_callback);
    glfwSetScrollCallback(main_window, &scroll_callback);
    glfwSetCharCallback(main_window, &char_callback);
    glfwSetFramebufferSizeCallback(main_window, &framebuffer_size_callback);
}

void Window::framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    auto *app = reinterpret_cast<Window *>(glfwGetWindowUserPointer(window));
    app->framebuffer_resized = true;
    app->window_width = static_cast<uint32_t>(width);
    app->window_height = static_cast<uint32_t>(height);

    ImGui_ImplGlfw_WindowFocusCallback(window, GLFW_TRUE);
}

void Window::reset_framebuffer_has_changed() { this->framebuffer_resized = false; }

void Window::key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);

    auto *the_window = static_cast<Window *>(glfwGetWindowUserPointer(window));
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard) { return; }

    Kataglyphis::Frontend::handle_key_callback(window, the_window->input_state.keys.data(), key, action);
}

void Window::mouse_callback(GLFWwindow *window, double x_pos, double y_pos)
{
    ImGui_ImplGlfw_CursorPosCallback(window, x_pos, y_pos);

    auto *the_window = static_cast<Window *>(glfwGetWindowUserPointer(window));
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) { return; }

    Kataglyphis::Frontend::handle_mouse_callback(window,
      the_window->input_state.last_x,
      the_window->input_state.last_y,
      the_window->input_state.x_change,
      the_window->input_state.y_change,
      the_window->input_state.mouse_first_moved,
      x_pos,
      y_pos);
}

void Window::mouse_button_callback(GLFWwindow *window, int button, int action, int mods)
{
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

    auto *the_window = static_cast<Window *>(glfwGetWindowUserPointer(window));
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) { return; }

    Kataglyphis::Frontend::handle_mouse_button_callback(
      window, the_window->input_state.mouse_first_moved, button, action, mouse_callback);
}

void Window::scroll_callback(GLFWwindow *window, double x_offset, double y_offset)
{
    ImGui_ImplGlfw_ScrollCallback(window, x_offset, y_offset);
}

void Window::char_callback(GLFWwindow *window, unsigned int codepoint)
{
    ImGui_ImplGlfw_CharCallback(window, codepoint);
}

Window::~Window() = default;
