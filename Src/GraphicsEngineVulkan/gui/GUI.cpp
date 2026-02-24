module;

#include "common/Utilities.hpp"

#include "../../shared/imgui/KataglyphisImGuiFonts.hpp"
#include "../../shared/imgui/KataglyphisImGuiStyle.hpp"

#include <cstdint>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan_core.h>

module kataglyphis.vulkan.gui;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.queue_family_indices;
import kataglyphis.vulkan.window;

using namespace Kataglyphis::Frontend;

namespace {
void applyKataglyphisDarkTheme() { Kataglyphis::Frontend::applyKataglyphisImGuiDarkTheme(); }
}// namespace

GUI::GUI(Window *window) : window(window) {}

void GUI::initializeVulkanContext(VulkanDevice *device,
  const VkInstance &instance,
  const VkRenderPass &post_render_pass,
  const VkCommandPool &graphics_command_pool,
  uint32_t image_count)
{
    this->device = device;
    (void)graphics_command_pool;

    create_gui_context(window, instance, post_render_pass, image_count);
    // create_fonts_and_upload(graphics_command_pool);
}

void GUI::setUserSelectionForRRT(bool rrtCapabilitiesAvailable)
{
    renderUserSelectionForRRT = rrtCapabilitiesAvailable;
}

void GUI::render()
{
    // Start the Dear ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // ImGui::ShowDemoWindow();

    // render your GUI
    ImGui::Begin("GUI v1.4.4");

    if (ImGui::CollapsingHeader("Hot shader reload")) {
        if (ImGui::Button("All shader!")) { guiRendererSharedVars.shader_hot_reload_triggered = true; }
    }

    ImGui::Separator();

    static int e = 0;
    ImGui::RadioButton("Rasterizer", &e, 0);
    ImGui::SameLine();
    if (renderUserSelectionForRRT) {
        ImGui::RadioButton("Raytracing", &e, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Path tracing", &e, 2);
    }

    switch (e) {
    case 0:
        guiRendererSharedVars.raytracing = false;
        guiRendererSharedVars.pathTracing = false;
        break;
    case 1:
        guiRendererSharedVars.raytracing = true;
        guiRendererSharedVars.pathTracing = false;
        break;
    case 2:
        guiRendererSharedVars.raytracing = false;
        guiRendererSharedVars.pathTracing = true;
        break;
    }
    // ImGui::Checkbox("Ray tracing", &guiRendererSharedVars.raytracing);

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Graphic Settings")) {
        if (ImGui::TreeNode("Directional Light")) {
            ImGui::Separator();
            ImGui::SliderFloat("Ambient intensity", &guiSceneSharedVars.direcional_light_radiance, 0.0F, 50.0F);
            ImGui::Separator();
            // Edit a color (stored as ~4 floats)
            ImGui::ColorEdit3("Directional Light Color", guiSceneSharedVars.directional_light_color);
            ImGui::Separator();
            ImGui::SliderFloat3("Light Direction", guiSceneSharedVars.directional_light_direction, -1.F, 1.0F);

            ImGui::TreePop();
        }
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("GUI Settings")) {
        ImGuiStyle &style = ImGui::GetStyle();

        if (ImGui::SliderFloat("Frame Rounding", &style.FrameRounding, 0.0F, 12.0F, "%.0f")) {
            style.GrabRounding = style.FrameRounding;// Make GrabRounding always the
                                                     // same value as FrameRounding
        }
        {
            bool border = (style.FrameBorderSize > 0.0F);
            if (ImGui::Checkbox("FrameBorder", &border)) { style.FrameBorderSize = border ? 1.0F : 0.0F; }
        }
        ImGui::SliderFloat("WindowRounding", &style.WindowRounding, 0.0F, 12.0F, "%.0f");
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("KEY Bindings")) {
        ImGui::Text("WASD for moving Forward, backward and to the side\n QE for rotating ");
    }

    ImGui::Separator();

    ImGui::Text(
      "Application average %.3f ms/frame (%.1f FPS)", 1000.0F / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

    ImGui::End();
}

void GUI::cleanUp()
{
    // clean up of GUI stuff
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(device->getLogicalDevice(), gui_descriptor_pool, nullptr);
}

void GUI::create_gui_context(Window *window,
  const VkInstance &instance,
  const VkRenderPass &post_render_pass,
  uint32_t image_count)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;

    Kataglyphis::Frontend::configureKataglyphisImGuiFonts(io);

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;// Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableSetMousePos;
    io.WantCaptureMouse = true;
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad
    // Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    applyKataglyphisDarkTheme();

    ImGui_ImplGlfw_InitForVulkan(window->get_window(), false);

    // Create Descriptor Pool
    VkDescriptorPoolSize gui_pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 10 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 10 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 10 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 10 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 10 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 } };

    VkDescriptorPoolCreateInfo gui_pool_info = {};
    gui_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    gui_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    gui_pool_info.maxSets = 10 * IM_ARRAYSIZE(gui_pool_sizes);
    gui_pool_info.poolSizeCount = static_cast<uint32_t> IM_ARRAYSIZE(gui_pool_sizes);
    gui_pool_info.pPoolSizes = gui_pool_sizes;

    VkResult const result =
      vkCreateDescriptorPool(device->getLogicalDevice(), &gui_pool_info, nullptr, &gui_descriptor_pool);
    ASSERT_VULKAN(result, "Failed to create a gui descriptor pool!")

    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices const indices = device->getQueueFamilies();

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = device->getPhysicalDevice();
    init_info.Device = device->getLogicalDevice();
    init_info.QueueFamily = indices.graphics_family;
    init_info.Queue = device->getGraphicsQueue();
    init_info.PipelineInfoMain.RenderPass = post_render_pass;
    init_info.DescriptorPool = gui_descriptor_pool;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.MinImageCount = 2;
    init_info.ImageCount = image_count;
    init_info.Allocator = VK_NULL_HANDLE;
    init_info.CheckVkResultFn = VK_NULL_HANDLE;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&init_info);// post_render_pass
}

GUI::~GUI() = default;
