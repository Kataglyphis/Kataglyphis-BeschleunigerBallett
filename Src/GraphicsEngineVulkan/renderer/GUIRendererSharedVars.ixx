module;

export module kataglyphis.vulkan.gui_renderer_shared_vars;

export namespace Kataglyphis::VulkanRendererInternals::FrontendShared {
enum class RasterizationMode { Forward, Deferred };

// Passes bracketed by GPU timestamp pairs in VulkanRenderer::record_commands.
// "Main" is forward/deferred rasterization plus the optional ray-/path-tracing
// stage that post-processes the raster result; "Post" includes the GUI draw
// (ImGui renders inside the post render pass).
inline constexpr int GPU_TIMED_PASS_COUNT = 5;
enum class GpuTimedPass : int { Clouds = 0, ShadowCascades, Main, Sky, Post };
inline constexpr const char *GPU_TIMED_PASS_NAMES[GPU_TIMED_PASS_COUNT] = {
    "Clouds (compute)",
    "Shadow cascades",
    "Main (raster/RT)",
    "Sky",
    "Post + GUI",
};

struct GpuTimings
{
    // false when the graphics queue family reports timestampValidBits == 0.
    bool supported = false;
    // Smoothed milliseconds per pass; < 0 means no sample yet or the pass is
    // currently disabled/not recorded.
    float pass_ms[GPU_TIMED_PASS_COUNT] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
};

struct GUIRendererSharedVars
{
    RasterizationMode rasterizationMode = RasterizationMode::Forward;

    bool raytracing = false;
    bool pathTracing = false;

    bool shader_hot_reload_triggered = false;

    // Per-pass GPU timings, written by the renderer, read by the GUI.
    GpuTimings gpuTimings;

    // path tracing vars
};
}// namespace Kataglyphis::VulkanRendererInternals::FrontendShared
