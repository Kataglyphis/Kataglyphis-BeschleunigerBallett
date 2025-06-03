#pragma once
#include <stdio.h>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

namespace KataglyphisRenderer::Frontend {
class Window
{
  public:
    Window();
    Window(uint32_t window_width, uint32_t window_height);

    // init glfw and its context ...
    int initialize();
    void cleanUp();

    // GETTER functions
    bool get_should_close() { return glfwWindowShouldClose(main_window); }
    float get_buffer_width() const { return (float)window_buffer_width; }
    float get_buffer_height() const { return (float)window_buffer_height; }
    float get_x_change();
    float get_y_change();
    GLFWwindow *get_window() { return main_window; }

    float get_height();
    float get_width();

    bool *get_keys() { return keys; }
    bool framebuffer_size_has_changed();
    void reset_framebuffer_has_changed();

    // SETTER functions
    void update_viewport();
    void set_buffer_size(float window_buffer_width, float window_buffer_height);

    ~Window();

  private:
    GLFWwindow *main_window;
    uint32_t window_width, window_height;
    // what key(-s) was/were pressed
    bool keys[1024];
    float last_x;
    float last_y;
    float x_change;
    float y_change;
    bool mouse_first_moved;
    bool framebuffer_resized;

    // buffers to store our window data to
    int window_buffer_width, window_buffer_height;

    // we need to start our window callbacks for interaction
    void init_callbacks();
    static void framebuffer_size_callback(GLFWwindow *window, int width, int height);

    // need to be static ...
    static void key_callback(GLFWwindow *window, int key, int code, int action, int mode);
    static void mouse_callback(GLFWwindow *window, double x_pos, double y_pos);
    static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods);
};
}// namespace KataglyphisRenderer::Frontend