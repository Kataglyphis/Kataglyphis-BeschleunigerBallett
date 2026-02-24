module;

#include <array>
#include <cstdio>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <iostream>
#include <print>
#include <utility>

module kataglyphis.opengl.window;

Window::Window() : window_width(800), window_height(600), x_change(0.0F), y_change(0.0F)
{
    // all keys non-pressed in the beginning
    for (bool &key : keys) { key = 0; }

    initialized = initialize() == 0;
}

// please use this constructor; never the standard
Window::Window(GLint window_width, GLint window_height)
  :

    window_width(window_width), window_height(window_height), x_change(0.0F), y_change(0.0F)
{
    // all keys non-pressed in the beginning
    for (bool &key : keys) { key = 0; }

    initialized = initialize() == 0;
}

auto Window::initialize() -> int
{
    if (glfwInit() == 0) {
        std::print("GLFW Init failed!");
        glfwTerminate();
        return 1;
    }

    auto const apply_window_hints = [](int major, int minor) {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
#ifdef NDEBUG
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, false);
#else
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, 1);
#endif
    };

    std::array<std::pair<int, int>, 4> const gl_versions = {
        std::pair{ 4, 6 }, std::pair{ 4, 5 }, std::pair{ 4, 4 }, std::pair{ 4, 3 }
    };

    for (auto const &[major, minor] : gl_versions) {
        apply_window_hints(major, minor);
        main_window =
          glfwCreateWindow(window_width, window_height, "\\__/ Epic graphics from hell \\__/", nullptr, nullptr);
        if (main_window != nullptr) {
            std::println("Created OpenGL context {}.{}", major, minor);
            break;
        }
    }

    if (main_window == nullptr) {
        char const *description = nullptr;
        auto const error_code = glfwGetError(&description);
        std::println("GLFW Window creation failed! Error code: {} message: {}",
          error_code,
          description != nullptr ? description : "no description");
        glfwTerminate();
        return 1;
    }

    // get buffer size information
    glfwGetFramebufferSize(main_window, &window_buffer_width, &window_buffer_height);

    // set context for GLEW to use
    glfwMakeContextCurrent(main_window);

    if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0) {
        std::cout << "Failed to initialize OpenGL context" << '\n';
        return -1;
    }

    // disabling frame limits
    glfwSwapInterval(0);

    // Handle key + mouse Input
    init_callbacks();
    glfwSetInputMode(main_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    glEnable(GL_DEPTH_TEST);

    // setup viewport size
    glViewport(0, 0, window_buffer_width, window_buffer_height);

    glfwSetWindowUserPointer(main_window, this);

    return 0;
}

void Window::update_viewport()
{
    glfwGetFramebufferSize(main_window, &window_buffer_width, &window_buffer_height);
    // setup viewport size
    glViewport(0, 0, window_buffer_width, window_buffer_height);
}

auto Window::get_x_change() -> GLfloat
{
    GLfloat const the_change = x_change;
    x_change = 0.0F;
    return the_change;
}

auto Window::get_y_change() -> GLfloat
{
    GLfloat const the_change = y_change;
    y_change = 0.0F;
    return the_change;
}

Window::~Window()
{
    glfwDestroyWindow(main_window);
    glfwTerminate();
}

void Window::init_callbacks()
{
    // TODO(jsh): remember this section for our later game logic
    // for the space ship to fly around
    glfwSetKeyCallback(main_window, key_callback);
    glfwSetMouseButtonCallback(main_window, mouse_button_callback);
    glfwSetFramebufferSizeCallback(main_window, framebuffer_size_callback);
}

void Window::framebuffer_size_callback(GLFWwindow * /*window*/, int /*width*/, int /*height*/) {}

void Window::key_callback(GLFWwindow *window, int key, int /*code*/, int action, int /*mode*/)
{
    auto *the_window = static_cast<Window *>(glfwGetWindowUserPointer(window));

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) { glfwSetWindowShouldClose(window, GL_TRUE); }

    if (key >= 0 && key < 1024) {
        if (action == GLFW_PRESS) {
            the_window->keys[key] = true;

        } else if (action == GLFW_RELEASE) {
            the_window->keys[key] = false;
        }
    }
}

void Window::mouse_callback(GLFWwindow *window, double x_pos, double y_pos)
{
    auto *the_window = static_cast<Window *>(glfwGetWindowUserPointer(window));

    // need to handle first occurance of a mouse moving event
    if (the_window->mouse_first_moved) {
        the_window->last_x = static_cast<float>(x_pos);
        the_window->last_y = static_cast<float>(y_pos);
        the_window->mouse_first_moved = false;
    }

    the_window->x_change = static_cast<float>(x_pos - the_window->last_x);
    // take care of correct substraction :)
    the_window->y_change = static_cast<float>(the_window->last_y - y_pos);

    // update params
    the_window->last_x = static_cast<float>(x_pos);
    the_window->last_y = static_cast<float>(y_pos);
}

void Window::mouse_button_callback(GLFWwindow *window, int button, int action, int /*mods*/)
{

    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) {
        ImGuiIO &io = ImGui::GetIO();
        io.AddMouseButtonEvent(button, action != 0);
        return;
    }
    auto *the_window = static_cast<Window *>(glfwGetWindowUserPointer(window));

    if ((action == GLFW_PRESS) && (button == GLFW_MOUSE_BUTTON_RIGHT)) {
        glfwSetCursorPosCallback(window, mouse_callback);
    } else {
        the_window->mouse_first_moved = true;
        glfwSetCursorPosCallback(window, nullptr);
    }
}
