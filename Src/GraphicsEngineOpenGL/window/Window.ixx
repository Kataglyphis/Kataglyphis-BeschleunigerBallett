module;

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "../../shared/frontend/WindowInputState.hpp"

export module kataglyphis.opengl.window;

export class Window
{
  public:
    Window();
    Window(GLint window_width, GLint window_height);
    Window(const Window &) = delete;
    auto operator=(const Window &) -> Window & = delete;
    Window(Window &&) = delete;
    auto operator=(Window &&) -> Window & = delete;

    bool get_should_close() { return main_window == nullptr || glfwWindowShouldClose(main_window) != 0; }
    void swap_buffers()
    {
        if (main_window != nullptr) { glfwSwapBuffers(main_window); }
    }

    bool is_initialized() const { return initialized; }

    // init glfw and its context ...
    int initialize();

    void update_viewport();

    GLuint get_buffer_width() const { return static_cast<GLuint>(window_buffer_width); }
    GLuint get_buffer_height() const { return static_cast<GLuint>(window_buffer_height); }

    GLfloat get_x_change();
    GLfloat get_y_change();

    GLFWwindow *get_window() { return main_window; }

    bool *get_keys() { return input_state.keys.data(); }

    ~Window();

  private:
    GLFWwindow *main_window{};
    bool initialized{ false };

    GLint window_width, window_height;
    // what key(-s) was/were pressed
    Kataglyphis::Frontend::WindowInputState input_state;

    // buffers to store our window data to
    GLint window_buffer_width{}, window_buffer_height{};

    // we need to start our window callbacks for interaction
    void init_callbacks();
    static void framebuffer_size_callback(GLFWwindow *window, int width, int height);
    // after window resizing, update framebuffers

    // need to be static ...
    static void key_callback(GLFWwindow *window, int key, int code, int action, int mode);
    static void mouse_callback(GLFWwindow *window, double x_pos, double y_pos);
    static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods);
};
