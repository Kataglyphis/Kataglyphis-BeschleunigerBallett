module;

#include <memory>
#include <vulkan/vulkan.h>

export module kataglyphis.vulkan.gui;

import kataglyphis.vulkan.command_buffer_manager;
import kataglyphis.vulkan.device;
import kataglyphis.vulkan.gui_renderer_shared_vars;
import kataglyphis.shared.frontend.gui_scene_shared_vars;
import kataglyphis.vulkan.window;

export namespace Kataglyphis::Frontend {
class GUI
{
  public:
    GUI(Window *window);

    void initializeVulkanContext(VulkanDevice *device,
      const VkInstance &instance,
      const VkRenderPass &post_render_pass,
      const VkCommandPool &graphics_command_pool,
      uint32_t image_count);

    Kataglyphis::Frontend::GUISceneSharedVars getGuiSceneSharedVars() { return guiSceneSharedVars; };
    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars &getGuiRendererSharedVars()
    {
        return guiRendererSharedVars;
    };

    void setUserSelectionForRRT(bool rrtCapabilitiesAvailable);

    void render();

    void cleanUp();

    ~GUI();

  private:
    void create_gui_context(Window *window,
      const VkInstance &instance,
      const VkRenderPass &post_render_pass,
      uint32_t image_count);

    VulkanDevice *device{ VK_NULL_HANDLE };
    Window *window{ VK_NULL_HANDLE };
    VkDescriptorPool gui_descriptor_pool{ VK_NULL_HANDLE };
    Kataglyphis::VulkanRendererInternals::CommandBufferManager commandBufferManager;

    Kataglyphis::Frontend::GUISceneSharedVars guiSceneSharedVars;
    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars guiRendererSharedVars;

    bool renderUserSelectionForRRT = true;
};

}// namespace Kataglyphis::Frontend
