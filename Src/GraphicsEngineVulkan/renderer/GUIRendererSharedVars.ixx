module;

export module kataglyphis.vulkan.gui_renderer_shared_vars;

export namespace Kataglyphis::VulkanRendererInternals::FrontendShared {
enum class RasterizationMode { Forward, Deferred };

struct GUIRendererSharedVars
{
    RasterizationMode rasterizationMode = RasterizationMode::Forward;

    bool raytracing = false;
    bool pathTracing = false;

    bool shader_hot_reload_triggered = false;

    // path tracing vars
};
}// namespace Kataglyphis::VulkanRendererInternals::FrontendShared