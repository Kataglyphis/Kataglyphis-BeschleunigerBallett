module;

#include <cstdlib>
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <format>
#include <memory>

#include "spdlog/spdlog.h"

module kataglyphis.vulkan.app;

import kataglyphis.shared.frontend.frame_input;

import kataglyphis.vulkan.camera;
import kataglyphis.vulkan.gui;
import kataglyphis.vulkan.renderer;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.window;

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

        Kataglyphis::Frontend::update_frame_timing(delta_time, last_time);

        // handle events for the camera
        Kataglyphis::Frontend::process_camera_input(window.get(), camera.get(), delta_time);

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

    vulkan_renderer.cleanUp();
    window->cleanUp();

    return EXIT_SUCCESS;
}

Kataglyphis::App::~App() = default;
