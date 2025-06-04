#include "app/App.hpp"

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "gui/GUI.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "window/Window.hpp"

Kataglyphis::App::App() {}

int Kataglyphis::App::run()
{
    int window_width = 1200;
    int window_height = 768;

    float delta_time = 0.0f;
    float last_time = 0.0f;

    std::unique_ptr<Kataglyphis::Frontend::Window> window =
      std::make_unique<Kataglyphis::Frontend::Window>(window_width, window_height);
    std::unique_ptr<Scene> scene = std::make_unique<Scene>();
    std::unique_ptr<Kataglyphis::Frontend::GUI> gui = std::make_unique<Kataglyphis::Frontend::GUI>(window.get());
    std::unique_ptr<Camera> camera = std::make_unique<Camera>();

    Kataglyphis::VulkanRenderer vulkan_renderer{ window.get(), scene.get(), gui.get(), camera.get() };

    while (!window->get_should_close()) {
        // poll all events incoming from user
        glfwPollEvents();

        // handle events for the camera
        camera->key_control(window->get_keys(), delta_time);
        camera->mouse_control(window->get_x_change(), window->get_y_change());

        float now = static_cast<float>(glfwGetTime());
        delta_time = now - last_time;
        last_time = now;

        scene->update_user_input(gui.get());

        vulkan_renderer.updateStateDueToUserInput(gui.get());
        vulkan_renderer.updateUniforms(scene.get(), camera.get(), window.get());

        //// retrieve updates from the UI
        gui->render();

        vulkan_renderer.drawFrame();
    }

    vulkan_renderer.finishAllRenderCommands();

    scene->cleanUp();
    gui->cleanUp();
    window->cleanUp();
    vulkan_renderer.cleanUp();

    return EXIT_SUCCESS;
}

Kataglyphis::App::~App() {}
