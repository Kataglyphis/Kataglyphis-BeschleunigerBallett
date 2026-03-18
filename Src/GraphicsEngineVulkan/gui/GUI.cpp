module;

#include "common/Utilities.hpp"

#include <array>
#include <cstdint>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.hpp>

module kataglyphis.vulkan.gui;

import kataglyphis.shared.frontend.common_gui_panels;
import kataglyphis.shared.imgui.fonts;
import kataglyphis.shared.imgui.style;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.queue_family_indices;
import kataglyphis.vulkan.window;

using namespace Kataglyphis::Frontend;

namespace {
void applyKataglyphisDarkTheme() { Kataglyphis::Frontend::applyKataglyphisImGuiDarkTheme(); }
}// namespace

GUI::GUI(Window *window) : window(window) {}

void GUI::initializeVulkanContext(VulkanDevice *device,
  const vk::Instance &instance,
  const vk::RenderPass &post_render_pass,
  const vk::CommandPool &graphics_command_pool,
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
    ImGui::Begin("GUI v" PROJECT_VERSION);

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

    Kataglyphis::Frontend::renderCommonGuiStyleSettings();

    ImGui::Separator();

    Kataglyphis::Frontend::renderCommonKeyBindings();

    ImGui::Separator();

    Kataglyphis::Frontend::renderCommonFrameStats();

    ImGui::End();
}

void GUI::cleanUp()
{
    // clean up of GUI stuff
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    device->getLogicalDevice().destroyDescriptorPool(gui_descriptor_pool);
}

void GUI::create_gui_context(Window *window,
  const vk::Instance &instance,
  const vk::RenderPass &post_render_pass,
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
    std::array<vk::DescriptorPoolSize, 11> gui_pool_sizes = { { { vk::DescriptorType::eSampler, 10 },
      { vk::DescriptorType::eCombinedImageSampler, 10 },
      { vk::DescriptorType::eSampledImage, 10 },
      { vk::DescriptorType::eStorageImage, 10 },
      { vk::DescriptorType::eUniformTexelBuffer, 10 },
      { vk::DescriptorType::eStorageTexelBuffer, 10 },
      { vk::DescriptorType::eUniformBuffer, 10 },
      { vk::DescriptorType::eStorageBuffer, 10 },
      { vk::DescriptorType::eUniformBufferDynamic, 10 },
      { vk::DescriptorType::eStorageBufferDynamic, 10 },
      { vk::DescriptorType::eInputAttachment, 100 } } };

    vk::DescriptorPoolCreateInfo gui_pool_info{};
    gui_pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    gui_pool_info.maxSets = 10 * static_cast<uint32_t>(gui_pool_sizes.size());
    gui_pool_info.poolSizeCount = static_cast<uint32_t>(gui_pool_sizes.size());
    gui_pool_info.pPoolSizes = gui_pool_sizes.data();

    auto pool_result = device->getLogicalDevice().createDescriptorPool(gui_pool_info);
    ASSERT_VULKAN(VkResult(pool_result.result), "Failed to create a gui descriptor pool!")
    gui_descriptor_pool = pool_result.value;

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
