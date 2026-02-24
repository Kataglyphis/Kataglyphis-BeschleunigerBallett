#include <gtest/gtest.h>

#include <filesystem>
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <sstream>
#include <vector>

#include <cstdio>
#include <iostream>

#include <memory>
#include <string>

import kataglyphis.opengl.scene;
import kataglyphis.opengl.renderer;

// Demonstrate some basic assertions.
TEST(RendererTest, BasicSetup)
{

    GLint window_width = 1200;
    GLint window_height = 800;

    // make sure ti initialize window first
    // this will create opengl context!
    // std::shared_ptr<Window> main_window =
    //     std::make_shared<Window>(window_width, window_height);

    // DebugApp debugCallbacks;

    // Renderer renderer(window_width, window_height);

    // GUI gui;
    // gui.init(main_window);

    // LoadingScreen loading_screen;
    // loading_screen.init();

    // std::shared_ptr<Camera> main_camera = std::make_shared<Camera>();

    // std::shared_ptr<Scene> scene =
    //     std::make_shared<Scene>(main_camera, main_window);

    // glm::mat4 projection_matrix = glm::perspectiveFov(
    //     glm::radians(main_camera->get_fov()), (GLfloat)window_width,
    //     (GLfloat)window_height, main_camera->get_near_plane(),
    //     main_camera->get_far_plane());

    // GLfloat delta_time = 0.0f;
    // GLfloat last_time = 0.0f;

    // GLfloat now = (float)glfwGetTime();
    // delta_time = now - last_time;
    // last_time = now;
}

TEST(ObjLoaderTest, blob)
{
    glm::vec3 sponza_offset = glm::vec3(0.f, 0.0f, 0.0f);
    //   GLfloat sponza_scale = 10.f;
    //   Rotation sponza_rot;
    //   sponza_rot.degrees = 0.0f;
    //   sponza_rot.axis = glm::vec3(0.0f, 1.0f, 0.0f);

    //   std::stringstream modelFile;
    //   std::filesystem::path cwd = std::filesystem::current_path();
    //   modelFile << cwd.string();
    //   modelFile << RELATIVE_RESOURCE_PATH << "Models/dinosaurs.obj";

    //   ASSERT_EQ(static_cast<uint32_t>(gameObjects.size()), 0);
    //   std::shared_ptr<GameObject> sponza = std::make_shared<GameObject>(
    //       modelFile.str(), sponza_offset, sponza_scale, sponza_rot);
    //   gameObjects.push_back(sponza);
    //   ASSERT_EQ(static_cast<uint32_t>(gameObjects.size()), 1);
}