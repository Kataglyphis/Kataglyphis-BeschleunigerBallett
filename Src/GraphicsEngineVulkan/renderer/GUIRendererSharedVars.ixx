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

struct VisibilityStats
{
    /// Meshes actually drawn by the raster path last frame.
    unsigned int meshes_drawn = 0;
    /// Meshes the raster path considered, culled or not.
    unsigned int meshes_total = 0;
};

struct GUIRendererSharedVars
{
    RasterizationMode rasterizationMode = RasterizationMode::Forward;

    bool raytracing = false;
    bool pathTracing = false;

    bool shader_hot_reload_triggered = false;
    /// CPU frustum culling for the raster paths. A switch rather than a
    /// constant because culling is the one optimisation that can silently
    /// delete visible geometry - when something goes missing, being able to
    /// turn it off tells you in one click whether culling is the cause.
    /// Never applied to the shadow pass; see Rasterizer::recordCommands.
    bool frustum_culling_enabled = true;

    // Per-pass GPU timings, written by the renderer, read by the GUI.
    GpuTimings gpuTimings;

    /// Meshes submitted vs considered by the last recorded frame, written by
    /// the renderer and read by the GUI.
    ///
    /// Culling is otherwise invisible: it has no visual signature when it is
    /// working and none when it is wrong either - geometry simply is not
    /// there. These two numbers are what turn "something is missing" into
    /// "culling dropped it", and they are also the only way to see whether
    /// culling is doing anything at all in a given scene.
    VisibilityStats visibility;

    // path tracing vars
};
}// namespace Kataglyphis::VulkanRendererInternals::FrontendShared
