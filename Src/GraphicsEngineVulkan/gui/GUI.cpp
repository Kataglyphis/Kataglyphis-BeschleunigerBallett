#include "gui/GUI.hpp"

#include "common/Utilities.hpp"
#include "renderer/QueueFamilyIndices.hpp"
#include "vulkan_base/VulkanDevice.hpp"

#include "renderer/VulkanRendererConfig.hpp"

#include <array>
#include <filesystem>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

using namespace Kataglyphis::Frontend;

namespace {
std::filesystem::path resolveImGuiFontDirectory(const std::filesystem::path &cwd)
{
    const auto fromMacro = (cwd / std::filesystem::path(RELATIVE_IMGUI_FONTS_PATH)).lexically_normal();
    if (std::filesystem::exists(fromMacro)) {
        return fromMacro;
    }

    std::filesystem::path current = cwd;
    for (int depth = 0; depth < 8; ++depth) {
        const auto candidate = (current / "ExternalLib/IMGUI/misc/fonts").lexically_normal();
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }

        if (!current.has_parent_path()) {
            break;
        }
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    return {};
}

bool addFontIfAvailable(ImFontAtlas *fonts, const std::filesystem::path &fontPath, float sizePixels)
{
    if (!std::filesystem::exists(fontPath)) {
        return false;
    }

    return fonts->AddFontFromFileTTF(fontPath.string().c_str(), sizePixels) != nullptr;
}

void applyKataglyphisDarkTheme()
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    constexpr ImVec4 primary = ImVec4(0.4118f, 0.9412f, 0.6824f, 1.00f);
    constexpr ImVec4 primaryHover = ImVec4(0.4900f, 0.9600f, 0.7400f, 1.00f);
    constexpr ImVec4 primaryActive = ImVec4(0.3200f, 0.8600f, 0.6100f, 1.00f);

    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.97f, 0.96f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.46f, 0.50f, 0.48f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.98f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.01f, 0.01f, 0.01f, 0.98f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.03f, 0.03f, 0.03f, 0.99f);
    colors[ImGuiCol_Border] = ImVec4(0.15f, 0.19f, 0.17f, 1.00f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(primary.x, primary.y, primary.z, 0.32f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(primary.x, primary.y, primary.z, 0.44f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.10f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.96f);

    colors[ImGuiCol_MenuBarBg] = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.18f, 0.20f, 0.19f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(primary.x, primary.y, primary.z, 0.70f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(primary.x, primary.y, primary.z, 0.95f);

    colors[ImGuiCol_CheckMark] = primary;
    colors[ImGuiCol_SliderGrab] = primary;
    colors[ImGuiCol_SliderGrabActive] = primaryActive;

    colors[ImGuiCol_Button] = ImVec4(primary.x, primary.y, primary.z, 0.26f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(primaryHover.x, primaryHover.y, primaryHover.z, 0.52f);
    colors[ImGuiCol_ButtonActive] = ImVec4(primaryActive.x, primaryActive.y, primaryActive.z, 0.72f);

    colors[ImGuiCol_Header] = ImVec4(primary.x, primary.y, primary.z, 0.24f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(primaryHover.x, primaryHover.y, primaryHover.z, 0.44f);
    colors[ImGuiCol_HeaderActive] = ImVec4(primaryActive.x, primaryActive.y, primaryActive.z, 0.60f);

    colors[ImGuiCol_Separator] = ImVec4(0.17f, 0.20f, 0.18f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(primary.x, primary.y, primary.z, 0.66f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(primary.x, primary.y, primary.z, 0.90f);

    colors[ImGuiCol_ResizeGrip] = ImVec4(primary.x, primary.y, primary.z, 0.30f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(primaryHover.x, primaryHover.y, primaryHover.z, 0.55f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(primaryActive.x, primaryActive.y, primaryActive.z, 0.75f);

    colors[ImGuiCol_Tab] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(primaryHover.x, primaryHover.y, primaryHover.z, 0.58f);
    colors[ImGuiCol_TabSelected] = ImVec4(primary.x, primary.y, primary.z, 0.46f);
    colors[ImGuiCol_TabSelectedOverline] = primary;
    colors[ImGuiCol_TabDimmed] = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(primary.x, primary.y, primary.z, 0.30f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(primary.x, primary.y, primary.z, 0.72f);

    colors[ImGuiCol_PlotLines] = ImVec4(0.65f, 0.75f, 0.70f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = primary;
    colors[ImGuiCol_PlotHistogram] = primary;
    colors[ImGuiCol_PlotHistogramHovered] = primaryHover;

    colors[ImGuiCol_TextSelectedBg] = ImVec4(primary.x, primary.y, primary.z, 0.40f);
    colors[ImGuiCol_DragDropTarget] = primary;
    colors[ImGuiCol_NavCursor] = primary;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(primary.x, primary.y, primary.z, 0.75f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.04f, 0.08f, 0.06f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.15f, 0.24f, 0.20f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.10f, 0.16f, 0.13f, 1.00f);

    style.WindowRounding = 10.0f;
    style.FrameRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.ScrollbarRounding = 10.0f;
    style.TabRounding = 8.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.IndentSpacing = 14.0f;
    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(10.0f, 5.0f);
    style.ItemSpacing = ImVec2(9.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(7.0f, 6.0f);
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 12.0f;
}
}// namespace

GUI::GUI(Window *window) { this->window = window; }

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
            ImGui::SliderFloat("Ambient intensity", &guiSceneSharedVars.direcional_light_radiance, 0.0f, 50.0f);
            ImGui::Separator();
            // Edit a color (stored as ~4 floats)
            ImGui::ColorEdit3("Directional Light Color", guiSceneSharedVars.directional_light_color);
            ImGui::Separator();
            ImGui::SliderFloat3("Light Direction", guiSceneSharedVars.directional_light_direction, -1.f, 1.0f);

            ImGui::TreePop();
        }
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("GUI Settings")) {
        ImGuiStyle &style = ImGui::GetStyle();

        if (ImGui::SliderFloat("Frame Rounding", &style.FrameRounding, 0.0f, 12.0f, "%.0f")) {
            style.GrabRounding = style.FrameRounding;// Make GrabRounding always the
                                                     // same value as FrameRounding
        }
        {
            bool border = (style.FrameBorderSize > 0.0f);
            if (ImGui::Checkbox("FrameBorder", &border)) { style.FrameBorderSize = border ? 1.0f : 0.0f; }
        }
        ImGui::SliderFloat("WindowRounding", &style.WindowRounding, 0.0f, 12.0f, "%.0f");
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("KEY Bindings")) {
        ImGui::Text("WASD for moving Forward, backward and to the side\n QE for rotating ");
    }

    ImGui::Separator();

    ImGui::Text(
      "Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

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

void GUI::create_gui_context(
    Window *window, const VkInstance &instance, const VkRenderPass &post_render_pass, uint32_t image_count)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;

    float size_pixels = 18;

    const std::filesystem::path fontDir = resolveImGuiFontDirectory(std::filesystem::current_path());
    bool hasCustomFont = false;
    if (!fontDir.empty()) {
        const std::array<std::filesystem::path, 6> fontFiles = {
            fontDir / "Roboto-Medium.ttf",
            fontDir / "Cousine-Regular.ttf",
            fontDir / "DroidSans.ttf",
            fontDir / "Karla-Regular.ttf",
            fontDir / "ProggyClean.ttf",
            fontDir / "ProggyTiny.ttf"
        };

        for (const auto &fontPath : fontFiles) {
            hasCustomFont = addFontIfAvailable(io.Fonts, fontPath, size_pixels) || hasCustomFont;
        }
    }

    if (!hasCustomFont) {
        io.Fonts->AddFontDefault();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1);
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
    gui_pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(gui_pool_sizes);
    gui_pool_info.pPoolSizes = gui_pool_sizes;

    VkResult result = vkCreateDescriptorPool(device->getLogicalDevice(), &gui_pool_info, nullptr, &gui_descriptor_pool);
    ASSERT_VULKAN(result, "Failed to create a gui descriptor pool!")

    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices indices = device->getQueueFamilies();

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

GUI::~GUI() {}
