#include "app/App.hpp"
#include "scene/Camera.hpp"
#include "scene/Scene.hpp"

#include <cstdlib>
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <memory>

#include "gui/GUI.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "window/Window.hpp"
#include "spdlog/spdlog.h"

Kataglyphis::App::App() = default;

auto Kataglyphis::App::run() -> int
{
    int const window_width = 1200;
    int const window_height = 768;

    float delta_time = 0.0F;
    float last_time = 0.0F;

    std::unique_ptr<Kataglyphis::Frontend::Window> const window =
      std::make_unique<Kataglyphis::Frontend::Window>(window_width, window_height);
    std::unique_ptr<Scene> const scene = std::make_unique<Scene>();
    std::unique_ptr<Kataglyphis::Frontend::GUI> const gui = std::make_unique<Kataglyphis::Frontend::GUI>(window.get());
    std::unique_ptr<Camera> const camera = std::make_unique<Camera>();

    Kataglyphis::VulkanRenderer vulkan_renderer{ window.get(), scene.get(), gui.get(), camera.get() };

    while (!window->get_should_close()) {
        // poll all events incoming from user
        glfwPollEvents();

        // handle events for the camera
        camera->key_control(window->get_keys(), delta_time);
        camera->mouse_control(window->get_x_change(), window->get_y_change());

        auto const now = static_cast<float>(glfwGetTime());
        delta_time = now - last_time;
        last_time = now;

        scene->update_user_input(gui.get());

        vulkan_renderer.updateStateDueToUserInput(gui.get());
        vulkan_renderer.updateUniforms(scene.get(), camera.get(), window.get());

        //// retrieve updates from the UI
        gui->render();

        vulkan_renderer.drawFrame();
    }

    if (!vulkan_renderer.hasDeviceLost()) { vulkan_renderer.finishAllRenderCommands(); }

    if (!vulkan_renderer.hasDeviceLost()) {
      scene->cleanUp();
      gui->cleanUp();
    } else {
      spdlog::warn("Skipping scene/gui Vulkan teardown because device is lost.");
    }
    window->cleanUp();
    vulkan_renderer.cleanUp();

    return EXIT_SUCCESS;
}

Kataglyphis::App::~App() = default;
