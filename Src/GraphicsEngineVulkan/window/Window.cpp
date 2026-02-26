module;
#include "GLFW/glfw3.h"
#include "spdlog/spdlog.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <imgui.h>

#include <print>
#include <vulkan/vulkan_core.h>

module kataglyphis.vulkan.window;

import kataglyphis.shared.frontend.window_input_callbacks;

using namespace Kataglyphis::Frontend;
// GLFW Callback functions
static void onErrorCallback(int error, const char *description)
{
    std::println(stderr, "GLFW Error {}: {}", error, description);
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
        std::print("GLFW Init failed!");
        glfwTerminate();
        return 1;
    }

    if (glfwVulkanSupported() == 0) { spdlog::error("No Vulkan Supported!"); }

    // allow it to resize
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // retrieve new window
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    main_window =
      glfwCreateWindow(window_width, window_height, "\\__/ Epic graphics from hell \\__/ ", nullptr, nullptr);

    if (main_window == nullptr) {
        std::print("GLFW Window creation failed!");
        glfwTerminate();
        return 1;
    }

    // get buffer size information
    glfwGetFramebufferSize(main_window, &window_buffer_width, &window_buffer_height);

    init_callbacks();

    return 0;
}

void Window::cleanUp()
{
    glfwDestroyWindow(main_window);
    glfwTerminate();
}

void Window::update_viewport() { glfwGetFramebufferSize(main_window, &window_buffer_width, &window_buffer_height); }

void Window::set_buffer_size(float window_buffer_width, float window_buffer_height)
{
    this->window_buffer_width = window_buffer_width;
    this->window_buffer_height = window_buffer_height;
}

auto Window::get_x_change() -> float
{
    return Kataglyphis::Frontend::consume_axis_delta(input_state.x_change);
}

auto Window::get_y_change() -> float
{
    return Kataglyphis::Frontend::consume_axis_delta(input_state.y_change);
}

auto Window::get_height() const -> float { return static_cast<float>(window_height); }

auto Window::get_width() const -> float { return static_cast<float>(window_width); }

auto Window::framebuffer_size_has_changed() const -> bool { return framebuffer_resized; }

void Window::init_callbacks()
{
    // TODO(jsh): remember this section for our later game logic
    // for the space ship to fly around
    glfwSetWindowUserPointer(main_window, this);
    glfwSetKeyCallback(main_window, &key_callback);
    glfwSetMouseButtonCallback(main_window, &mouse_button_callback);
    glfwSetFramebufferSizeCallback(main_window, &framebuffer_size_callback);
}

void Window::framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    auto *app = reinterpret_cast<Window *>(glfwGetWindowUserPointer(window));
    app->framebuffer_resized = true;
    app->window_width = width;
    app->window_height = height;
}

void Window::reset_framebuffer_has_changed() { this->framebuffer_resized = false; }

void Window::key_callback(GLFWwindow *window, int key, int /*code*/, int action, int /*mode*/)
{
    auto *the_window = static_cast<Window *>(glfwGetWindowUserPointer(window));
    Kataglyphis::Frontend::handle_key_callback(window, the_window->input_state.keys.data(), key, action);
}

void Window::mouse_callback(GLFWwindow *window, double x_pos, double y_pos)
{
    auto *the_window = static_cast<Window *>(glfwGetWindowUserPointer(window));
    Kataglyphis::Frontend::handle_mouse_callback(window,
    the_window->input_state.last_x,
    the_window->input_state.last_y,
    the_window->input_state.x_change,
    the_window->input_state.y_change,
    the_window->input_state.mouse_first_moved,
      x_pos,
      y_pos);
}

void Window::mouse_button_callback(GLFWwindow *window, int button, int action, int /*mods*/)
{
    auto *the_window = static_cast<Window *>(glfwGetWindowUserPointer(window));
    Kataglyphis::Frontend::handle_mouse_button_callback(
    window, the_window->input_state.mouse_first_moved, button, action, mouse_callback);
}

Window::~Window() = default;
