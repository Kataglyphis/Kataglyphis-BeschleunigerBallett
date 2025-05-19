#pragma once

#include <memory>
// #define GLFW_INCLUDE_NONE
// #define GLFW_INCLUDE_VULKAN
// #include <GLFW/glfw3.h>

#include "renderer/CommandBufferManager.hpp"
#include "renderer/GUIRendererSharedVars.hpp"
#include "scene/GUISceneSharedVars.hpp"
#include "common/Globals.hpp"
#include "vulkan_base/VulkanDevice.hpp"
#include "window/Window.hpp"

class GUI
{
  public:
    GUI(Window *window);

    void initializeVulkanContext(VulkanDevice *device,
      const VkInstance &instance,
      const VkRenderPass &post_render_pass,
      const VkCommandPool &graphics_command_pool);

    GUISceneSharedVars getGuiSceneSharedVars() { return guiSceneSharedVars; };
    GUIRendererSharedVars &getGuiRendererSharedVars() { return guiRendererSharedVars; };

    void setUserSelectionForRRT(bool rrtCapabilitiesAvailable);

    void render();

    void cleanUp();

    ~GUI();

  private:
    void create_gui_context(Window *window, const VkInstance &instance, const VkRenderPass &post_render_pass);

    void create_fonts_and_upload(const VkCommandPool &graphics_command_pool);

    VulkanDevice *device{ VK_NULL_HANDLE };
    Window *window{ VK_NULL_HANDLE };
    VkDescriptorPool gui_descriptor_pool{ VK_NULL_HANDLE };
    CommandBufferManager commandBufferManager;

    GUISceneSharedVars guiSceneSharedVars;
    GUIRendererSharedVars guiRendererSharedVars;

    bool renderUserSelectionForRRT = true;
};
