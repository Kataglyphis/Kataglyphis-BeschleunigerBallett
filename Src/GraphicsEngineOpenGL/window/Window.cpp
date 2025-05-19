#include "window/Window.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <iostream>
#include <stdio.h>

Window::Window()
    : window_width(800), window_height(600), x_change(0.0f), y_change(0.0f) {
  // all keys non-pressed in the beginning
  for (size_t i = 0; i < 1024; i++) {
    keys[i] = 0;
  }

  initialize();
}

// please use this constructor; never the standard
Window::Window(GLint window_width, GLint window_height)
    :

      window_width(window_width),
      window_height(window_height),
      x_change(0.0f),
      y_change(0.0f) {
  // all keys non-pressed in the beginning
  for (size_t i = 0; i < 1024; i++) {
    keys[i] = 0;
  }

  initialize();
}

int Window::initialize() {
  if (!glfwInit()) {
    printf("GLFW Init failed!");
    glfwTerminate();
    return 1;
  }

  // setup glfw window properties

  // lets work with nothing older than version 3
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);

  // core profile = no backward compatibility
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  // allow forward compatibility
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

  // allow it to resize
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

#ifdef NDEBUG
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, false);
#else
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);
#endif

  // retrieve new window
  main_window =
      glfwCreateWindow(window_width, window_height,
                       "\\__/ Epic graphics from hell \\__/", NULL, NULL);

  if (!main_window) {
    printf("GLFW Window creation failed!");
    glfwTerminate();
    return 1;
  }

  // get buffer size information
  glfwGetFramebufferSize(main_window, &window_buffer_width,
                         &window_buffer_height);

  // set context for GLEW to use
  glfwMakeContextCurrent(main_window);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cout << "Failed to initialize OpenGL context" << std::endl;
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

void Window::update_viewport() {
  glfwGetFramebufferSize(main_window, &window_buffer_width,
                         &window_buffer_height);
  // setup viewport size
  glViewport(0, 0, window_buffer_width, window_buffer_height);
}

GLfloat Window::get_x_change() {
  GLfloat the_change = x_change;
  x_change = 0.0f;
  return the_change;
}

GLfloat Window::get_y_change() {
  GLfloat the_change = y_change;
  y_change = 0.0f;
  return the_change;
}

Window::~Window() {
  glfwDestroyWindow(main_window);
  glfwTerminate();
}

void Window::init_callbacks() {
  // TODO: remember this section for our later game logic
  // for the space ship to fly around
  glfwSetKeyCallback(main_window, key_callback);
  glfwSetMouseButtonCallback(main_window, mouse_button_callback);
  glfwSetFramebufferSizeCallback(main_window, framebuffer_size_callback);
}

void Window::framebuffer_size_callback(GLFWwindow* window, int width,
                                       int height) {}

void Window::key_callback(GLFWwindow* window, int key, int code, int action,
                          int mode) {
  Window* the_window = static_cast<Window*>(glfwGetWindowUserPointer(window));

  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
    glfwSetWindowShouldClose(window, GL_TRUE);
  }

  if (key >= 0 && key < 1024) {
    if (action == GLFW_PRESS) {
      the_window->keys[key] = true;

    } else if (action == GLFW_RELEASE) {
      the_window->keys[key] = false;
    }
  }
}

void Window::mouse_callback(GLFWwindow* window, double x_pos, double y_pos) {
  Window* the_window = static_cast<Window*>(glfwGetWindowUserPointer(window));

  // need to handle first occurance of a mouse moving event
  if (the_window->mouse_first_moved) {
    the_window->last_x = (float)x_pos;
    the_window->last_y = (float)y_pos;
    the_window->mouse_first_moved = false;
  }

  the_window->x_change = (float)(x_pos - the_window->last_x);
  // take care of correct substraction :)
  the_window->y_change = (float)(the_window->last_y - y_pos);

  // update params
  the_window->last_x = (float)x_pos;
  the_window->last_y = (float)y_pos;
}

void Window::mouse_button_callback(GLFWwindow* window, int button, int action,
                                   int mods) {

  if (ImGui::GetCurrentContext() != nullptr &&
      ImGui::GetIO().WantCaptureMouse) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseButtonEvent(button, action);
    return;
  }
  Window* the_window = static_cast<Window*>(glfwGetWindowUserPointer(window));

  if ((action == GLFW_PRESS) && (button == GLFW_MOUSE_BUTTON_RIGHT)) {
    glfwSetCursorPosCallback(window, mouse_callback);
  } else {
    the_window->mouse_first_moved = true;
    glfwSetCursorPosCallback(window, NULL);
  }
}
