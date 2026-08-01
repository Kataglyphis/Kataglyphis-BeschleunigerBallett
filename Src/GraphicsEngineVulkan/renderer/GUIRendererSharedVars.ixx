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

// Machine-readable identifiers for the same passes, used as JSON keys by the
// KATAGLYPHIS_GPU_TIMING_JSON export. Separate from the display names above:
// those may be reworded for the GUI at will, while these are diffed between
// runs by tooling and must stay stable.
inline constexpr const char *GPU_TIMED_PASS_EXPORT_NAMES[GPU_TIMED_PASS_COUNT] = {
    "Clouds",
    "ShadowCascades",
    "Main",
    "Sky",
    "Post",
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
    /// Shadow casters actually drawn by the cascade pass last frame. Culled
    /// against the UNION of the cascade frusta, not the camera frustum - a
    /// caster visible to any cascade counts as drawn.
    unsigned int shadow_casters_drawn = 0;
    /// Shadow casters the cascade pass considered, culled or not.
    unsigned int shadow_casters_total = 0;
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
    /// Gates ONLY the camera-frustum cull used by the two raster paths
    /// (VulkanRenderer::record_commands). The cascade pass culls
    /// unconditionally against the cascade frusta
    /// (CascadedShadowMap::recordCommands) and this switch does not turn
    /// that off.
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

    // Path-tracing quality controls. Both were hardcoded in the kernel (8/8,
    // with stale comments claiming 64/32); changing either mid-run resets the
    // accumulation history - a mean over two different estimators is biased.
    int pathTracingSamplesPerPixel = 8;
    int pathTracingMaxBounces = 8;
};
}// namespace Kataglyphis::VulkanRendererInternals::FrontendShared
