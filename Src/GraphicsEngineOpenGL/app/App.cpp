// include ability to execute threads
// clang-format off
// you must include glad before glfw!
// therefore disable clang-format for this section
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/trigonometric.hpp>

#include <iostream>
#include <memory>
#include <thread>

#include "debug/DebugApp.hpp"

import kataglyphis.opengl.camera;
import kataglyphis.opengl.scene;
import kataglyphis.opengl.renderer;
import kataglyphis.opengl.window;
import kataglyphis.opengl.gui;
import kataglyphis.opengl.loading_screen;

auto main() -> int
{

    // https://discourse.glfw.org/t/dont-want-a-console-window/401
    // for disabling console during production build under windows
    // The console window appears because you�re building your application for the console subsystem.
    // You need to build it for the Win32 subsystem and it will go away.
    // Note that the linker will then by default be looking for a WinMain() entry point, not a main() one.
    // If you wish to keep your main() entry point, set the entry point to mainCRTStartup under Advanced Linker Settings
    // (assuming you�re using Visual C++).

    bool loading_screen_finished = false;

    GLint window_width = 1200;
    GLint window_height = 800;

    // make sure ti initialize window first
    // this will create opengl context!
    std::shared_ptr<Window> const main_window = std::make_shared<Window>(window_width, window_height);

    if (!main_window->is_initialized()) {
        std::cerr << "Failed to initialize GLFW window/OpenGL context. Check graphics driver and display setup."
                  << '\n';
        return 1;
    }

    DebugApp const debugCallbacks;

    Renderer renderer(static_cast<GLuint>(window_width), static_cast<GLuint>(window_height));

    GUI gui;
    gui.init(main_window);

    LoadingScreen loading_screen;
    loading_screen.init();

    std::shared_ptr<Camera> const main_camera = std::make_shared<Camera>();

    std::shared_ptr<Scene> const scene = std::make_shared<Scene>(main_camera, main_window);

    // load scene in an other thread than the rendering thread; would block
    // otherwise
    std::thread t1 = scene->spwan();
    t1.detach();

    GLfloat delta_time = 0.0F;
    GLfloat last_time = 0.0F;

    // enable depth testing
    glEnable(GL_DEPTH_TEST);

    // Create and open a text file for logging console outpout/error
    // #if NDEBUG
    // #else
    //       assert(freopen("errorLog.txt", "w", stdout));
    //       assert(freopen("errorLog.txt", "w", stderr));
    // #endif

    while (!main_window->get_should_close()) {
        glViewport(0, 0, window_width, window_height);

        glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // we need the projection matrix, just use glm::perspective function
        glm::mat4 const projection_matrix = glm::perspectiveFov(glm::radians(main_camera->get_fov()),
          static_cast<GLfloat>(window_width),
          static_cast<GLfloat>(window_height),
          main_camera->get_near_plane(),
          main_camera->get_far_plane());

        // we should make the application independet of processor speed :)
        //  take time into account is crucial
        //  concept of delta time: https://bell0bytes.eu/keeping-track-of-time/
        auto const now = static_cast<float>(glfwGetTime());
        delta_time = now - last_time;
        last_time = now;

        // poll all events incoming from user
        glfwPollEvents();

        // handle events for the camera
        main_camera->key_control(main_window->get_keys(), delta_time);
        main_camera->mouse_control(main_window->get_x_change(), main_window->get_y_change());

        if (scene->is_loaded()) {
            if (!loading_screen_finished) { loading_screen_finished = true; }

            if (!scene->get_context_setup()) { scene->setup_game_object_context(); }

            renderer.drawFrame(main_camera, scene, projection_matrix, delta_time);

        } else {
            // play the audio
            // SoundEngine->play2D("Audio/Red_Dead_Redemption_2 _Loading_Screen.mp3",
            // true); //
            loading_screen.render();
        }

        bool shader_hot_reload_triggered = false;
        gui.render(!scene->is_loaded(), scene->get_progress(), shader_hot_reload_triggered);

        if (shader_hot_reload_triggered) { renderer.reload_shader_programs(); }

        gui.update_user_input(scene);

        main_window->update_viewport();
        GLuint const new_window_width = main_window->get_buffer_width();
        GLuint const new_window_height = main_window->get_buffer_height();

        if (!(static_cast<GLint>(new_window_width) == window_width
              && static_cast<GLint>(new_window_height) == window_height)) {
            window_height = static_cast<GLint>(new_window_height);
            window_width = static_cast<GLint>(new_window_width);
            renderer.update_window_params(window_width, window_height);
        }

        main_window->swap_buffers();
    }

    return 0;
}
