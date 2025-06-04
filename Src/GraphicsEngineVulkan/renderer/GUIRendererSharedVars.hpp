namespace KataglyphisRenderer::VulkanRendererInternals::FrontendShared {
struct GUIRendererSharedVars
{
    bool raytracing = false;
    bool pathTracing = false;

    bool shader_hot_reload_triggered = false;

    // path tracing vars
};
}// namespace KataglyphisRenderer::VulkanRendererInternals::FrontendShared