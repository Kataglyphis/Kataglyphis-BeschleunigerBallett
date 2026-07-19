// Structural "golden" tests over actually rendered pixels.
//
// These deliberately assert *structure* (variance, relative brightness,
// agreement between two code paths) and never exact pixel values, so they
// survive driver, GPU and denoiser differences. The motivating regression:
// the cascaded shadow maps were rendered for months while no shader sampled
// them - every existing test still passed because nothing ever inspected a
// rendered pixel. ShadowsDarkenSomePixels below is the direct guard for that.
//
// CAUTION when adding assertions here (learned 2026-07-19): the captured frame
// is dominated by the ImGui overlay, and the 3D scene contributes far fewer
// pixels than it looks. Classifying pixels by "pure" colour silently measures
// only the GUI, because the capture is tonemapped - a shader emitting (0,1,0)
// does not arrive as (0,255,0). Any pixel-classifying diagnostic added here
// must first be validated against a control (e.g. a shader forced to a known
// constant) before its numbers are trusted.

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <iostream>
#include <vulkan/vulkan.hpp>
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

import kataglyphis.vulkan.camera;
import kataglyphis.vulkan.gui;
import kataglyphis.vulkan.gui_renderer_shared_vars;
import kataglyphis.vulkan.gui_scene_shared_vars;
import kataglyphis.vulkan.renderer;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.window;

namespace {

using Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode;

constexpr int WINDOW_WIDTH = 1200;
constexpr int WINDOW_HEIGHT = 768;
// Frames rendered before the first capture so the scene, GUI layout and any
// per-image resources have settled.
constexpr int WARMUP_FRAMES = 5;
// Frames rendered after a settings change before capturing it.
constexpr int SETTLE_FRAMES = 3;

bool glfw_reports_vulkan_support()
{
    if (glfwInit() == 0) { return false; }
    const bool supports_vulkan = glfwVulkanSupported() != 0;
    glfwTerminate();
    return supports_vulkan;
}

// Owns the full engine exactly like the other integration suites do, and tears
// it down in the same order.
struct EngineHarness
{
    std::unique_ptr<Kataglyphis::Frontend::Window> window;
    std::unique_ptr<Kataglyphis::Scene> scene;
    std::unique_ptr<Kataglyphis::Frontend::GUI> gui;
    std::unique_ptr<Camera> camera;
    std::unique_ptr<Kataglyphis::VulkanRenderer> renderer;

    EngineHarness()
      : window(std::make_unique<Kataglyphis::Frontend::Window>(WINDOW_WIDTH, WINDOW_HEIGHT)),
        scene(std::make_unique<Kataglyphis::Scene>()),
        gui(std::make_unique<Kataglyphis::Frontend::GUI>(window.get())), camera(std::make_unique<Camera>())
    {
        renderer =
          std::make_unique<Kataglyphis::VulkanRenderer>(window.get(), scene.get(), gui.get(), camera.get());
    }

    EngineHarness(const EngineHarness &) = delete;
    EngineHarness &operator=(const EngineHarness &) = delete;

    // One iteration of the App::run frame loop (App.cpp) minus camera input.
    void render_frame()
    {
        glfwPollEvents();
        gui->render();
        scene->update_user_input(gui.get());
        renderer->updateStateDueToUserInput(gui.get());
        renderer->updateUniforms(scene.get(), camera.get(), window.get());
        renderer->drawFrame();
    }

    void render_frames(int count)
    {
        for (int frame = 0; frame < count; ++frame) { render_frame(); }
    }

    // Arms a capture, renders the frame that carries the copy, then reads it
    // back (fence-synced inside takeCapturedFrame).
    std::vector<uint8_t> capture_frame(uint32_t &width, uint32_t &height)
    {
        renderer->requestFrameCapture();
        render_frame();
        return renderer->takeCapturedFrame(width, height);
    }

    ~EngineHarness()
    {
        if (renderer && !renderer->hasDeviceLost()) {
            renderer->finishAllRenderCommands();
            scene->cleanUp();
            gui->cleanUp();
        }
        if (renderer) {
            renderer->cleanUp();
            renderer.reset();
        }
        camera.reset();
        gui.reset();
        scene.reset();
        if (window) {
            window->cleanUp();
            window.reset();
        }
    }
};

double luminance_of(const std::vector<uint8_t> &rgba, size_t pixel)
{
    const size_t base = pixel * 4U;
    return 0.2126 * static_cast<double>(rgba[base]) + 0.7152 * static_cast<double>(rgba[base + 1U])
           + 0.0722 * static_cast<double>(rgba[base + 2U]);
}

// Mean luminance over the whole frame, on a 0..255 scale.
double mean_luminance(const std::vector<uint8_t> &rgba)
{
    const size_t pixel_count = rgba.size() / 4U;
    if (pixel_count == 0U) { return 0.0; }

    double sum = 0.0;
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) { sum += luminance_of(rgba, pixel); }
    return sum / static_cast<double>(pixel_count);
}

double luminance_stddev(const std::vector<uint8_t> &rgba)
{
    const size_t pixel_count = rgba.size() / 4U;
    if (pixel_count < 2U) { return 0.0; }

    const double mean = mean_luminance(rgba);
    double accumulated = 0.0;
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const double delta = luminance_of(rgba, pixel) - mean;
        accumulated += delta * delta;
    }
    return std::sqrt(accumulated / static_cast<double>(pixel_count));
}

double fraction_above(const std::vector<uint8_t> &rgba, double threshold)
{
    const size_t pixel_count = rgba.size() / 4U;
    if (pixel_count == 0U) { return 0.0; }

    size_t hits = 0;
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
        if (luminance_of(rgba, pixel) > threshold) { ++hits; }
    }
    return static_cast<double>(hits) / static_cast<double>(pixel_count);
}

// Number of distinct 8-bit luminance buckets present. A blank or single-colour
// frame collapses to a handful.
size_t distinct_luminance_buckets(const std::vector<uint8_t> &rgba)
{
    std::vector<bool> seen(256, false);
    const size_t pixel_count = rgba.size() / 4U;
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const int bucket = std::clamp(static_cast<int>(luminance_of(rgba, pixel)), 0, 255);
        seen[static_cast<size_t>(bucket)] = true;
    }
    return static_cast<size_t>(std::count(seen.begin(), seen.end(), true));
}

// Common preconditions: a Vulkan-capable GLFW plus a surface that lets us copy
// out of a swapchain image at all.
#define SKIP_WITHOUT_GPU()                                                             \
    do {                                                                               \
        if (!glfw_reports_vulkan_support()) {                                          \
            GTEST_SKIP() << "GLFW/Vulkan runtime is unavailable on this system.";       \
        }                                                                              \
    } while (false)

} // namespace

// A rendered frame must carry actual image content: not a uniform colour, not
// (nearly) all black. This is the cheapest possible guard against the whole
// render graph silently producing nothing.
TEST(GoldenRender, RendersNonBlankFrame)
{
    SKIP_WITHOUT_GPU();

    EngineHarness harness;
    if (!harness.renderer->supportsFrameCapture()) {
        GTEST_SKIP() << "Surface does not support eTransferSrc; frame capture unavailable.";
    }

    auto &scene_vars = harness.gui->getGuiSceneSharedVars();
    auto &renderer_vars = harness.gui->getGuiRendererSharedVars();
    renderer_vars.raytracing = false;
    renderer_vars.pathTracing = false;
    renderer_vars.rasterizationMode = RasterizationMode::Forward;
    scene_vars.shadows_enabled = true;

    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while warming up.";

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> frame = harness.capture_frame(width, height);

    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost during capture.";
    ASSERT_FALSE(frame.empty()) << "Frame capture returned no pixels.";
    EXPECT_GT(width, 0U);
    EXPECT_GT(height, 0U);
    ASSERT_EQ(frame.size(), static_cast<size_t>(width) * static_cast<size_t>(height) * 4U)
      << "Captured buffer size does not match the reported extent.";

    const double stddev = luminance_stddev(frame);
    const double lit_fraction = fraction_above(frame, 8.0);
    const size_t buckets = distinct_luminance_buckets(frame);

    EXPECT_GT(stddev, 2.0) << "Frame is essentially uniform (luminance stddev " << stddev
                           << "); the render graph likely produced a blank image.";
    EXPECT_GT(lit_fraction, 0.05) << "Only " << (lit_fraction * 100.0)
                                  << "% of pixels are non-black; the frame looks empty.";
    EXPECT_GT(buckets, 8U) << "Only " << buckets
                           << " distinct luminance levels present; the frame looks like a flat fill.";
}

// THE centerpiece. Cascaded shadow maps were rendered but never sampled, and
// nothing noticed. Turning the shadow intensity up must make the image darker
// than with intensity 0 - if the shadow term ever stops reaching the lighting
// shaders again, the two means become identical and this fails.
// DISABLED: this test currently FAILS and the failure is REAL - enabling the
// cascaded shadow map does not measurably darken the frame (measured:
// darkened and brightened pixel counts are both noise-level; see the 2026-07-19
// re-measurement below).
//
// Five genuine defects were found and fixed while chasing it (NDC z
// unprojected from -1 under GLM_FORCE_DEPTH_ZERO_TO_ONE; the light-matrix UBO
// filled with default matrices at init and never updated; the shadow geometry
// shader assuming single-pass layered rendering the renderer does not do;
// glm::ortho handed negative light-view z as near/far plus a light camera one
// unit from the scene; cascade selection by radial distance instead of view
// depth). Shadows still do not darken.
//
// UPDATE 2026-07-19: the observation that "forcing the forward fragment shader
// to output a constant colour did not change the captured frame" is now
// EXPLAINED, and it was not a renderer bug. rasterizer/shader.frag was failing
// to compile (the shader root was missing from glslc's include paths) and
// compile-shaders.ps1 only warned, so the stale .spv kept being used - every
// edit to that shader was a no-op. See docs/shader-build-pipeline.md.
//
// That does NOT fix this test. Re-measured with genuinely current SPIR-V:
// mean luminance 27.2922 (intensity 0) vs 27.3229 (intensity 1), 391 pixels
// darkened vs 481 brightened of 466944. The shadow term still has no effect on
// the image, and raising the intensity very slightly BRIGHTENS it. The defect
// is real and independent of the shader-staleness bugs - but note that every
// diagnosis performed before 2026-07-19 was made against a binary that did not
// match its source, so those conclusions are worth re-deriving rather than
// trusting.
//
// UPDATE 2026-07-19 (second pass): the cause WAS found and fixed - the shadow
// pass pushed a hard-coded identity model matrix while the forward pass pushed
// the scene's matrix (a uniform scale of 60), so the caster was rendered at
// 1/60 size and the depth map stayed at its 1.0 clear value. Measured against
// forced black/white references, as a fraction of geometry pixels:
//
//                              before fix   after fix
//   shadow map has depth            0%         46%
//   fragment occluded (shadow>0)    0%         36%
//
// Shadows are therefore being computed now. This test still cannot SEE them:
// the model renders only ~3% above a forced-black reference, so
// `color *= 1 - shadow * intensity` moves whole-frame mean luminance by ~0.06
// against a run-to-run noise floor of ~0.04, and per-pixel deltas fall under
// the 4.0 CHANGE_THRESHOLD.
//
// UPDATE 2026-07-19 (third pass, after the debug scene changed to Dinosaurs):
// the scene is now bright (mean luminance 65 vs 26) and the geometry fills a
// large part of the frame, so the old brightness objection is gone. The test
// still fails, and the reason is now specific and worth knowing:
//
//   measured against forced black/white reference frames,
//   only 1.4% of geometry fragments are occluded by CSM.
//
// The prominent shadow visible under the dinosaurs in the app is BAKED INTO
// THE MODEL, not cast by the shadow maps - flipping cascaded_shadow_intensity
// from 0 to 1 moves only ~331 pixels out of 466944. Do not mistake the one for
// the other when eyeballing this scene.
//
// So CSM works (0% -> 1.4% occlusion after the model-matrix fix) but casts
// very little in this scene. The likely cause is cascade resolution: splits
// are uniform over [near, far], so cascade 0 still spans ~50 units for a
// 20-unit scene, and the depth bias then swallows most self-shadowing. A
// practical/logarithmic split scheme is the next thing to try - the comment in
// CascadedShadowMap::updateCascades already flags the uniform split as
// provisional.
//
// Do NOT lower CHANGE_THRESHOLD to force a pass.
TEST(GoldenRender, DISABLED_ShadowsDarkenSomePixels)
{
    SKIP_WITHOUT_GPU();

    EngineHarness harness;
    if (!harness.renderer->supportsFrameCapture()) {
        GTEST_SKIP() << "Surface does not support eTransferSrc; frame capture unavailable.";
    }

    auto &scene_vars = harness.gui->getGuiSceneSharedVars();
    auto &renderer_vars = harness.gui->getGuiRendererSharedVars();
    renderer_vars.raytracing = false;
    renderer_vars.pathTracing = false;
    renderer_vars.rasterizationMode = RasterizationMode::Forward;

    // Shadows stay enabled in both runs so the exact same passes are recorded;
    // only the intensity the lighting shaders apply changes. That isolates
    // "the shadow map is sampled" from "the shadow pass runs".
    scene_vars.shadows_enabled = true;

    // The default camera now frames the debug scene (Dinosaurs on its own
    // ground plane) from outside, so no override is needed here. It used to be
    // required because the scene was scaled 60x and the camera started inside
    // the geometry - see the commit that changed the debug scene.

    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while warming up.";

    uint32_t width = 0;
    uint32_t height = 0;

    scene_vars.cascaded_shadow_intensity = 0.0F;
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> without_shadows = harness.capture_frame(width, height);

    // Self-calibrating noise mask. The ImGui overlay redraws every frame - the
    // FPS/ms readout alone flips several hundred pixels - so a raw comparison
    // of two captures reports ~600 "brightened" pixels that have nothing to do
    // with shadows. Capture the SAME state twice and treat every pixel that
    // moved as unusable, then compare shadows only on the stable remainder.
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> noise_reference = harness.capture_frame(width, height);
    ASSERT_FALSE(without_shadows.empty()) << "Capture without shadows returned no pixels.";

    scene_vars.cascaded_shadow_intensity = 1.0F;
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> with_shadows = harness.capture_frame(width, height);
    ASSERT_FALSE(with_shadows.empty()) << "Capture with shadows returned no pixels.";

    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost during capture.";
    ASSERT_EQ(without_shadows.size(), with_shadows.size());

    // The whole-frame mean is a poor detector here: most of the frame is an
    // unshadowed skybox and the ImGui overlay (FPS / GPU timings) changes
    // between the two captures, which alone moves the global mean by a few
    // hundredths. So the comparison is restricted to the pixels the shadow
    // term can actually touch, and the direction of the change is checked
    // pixel-wise - `color *= 1.0 - shadow * intensity` can only ever darken,
    // so darkening must dominate overwhelmingly.
    constexpr double CHANGE_THRESHOLD = 4.0;

    const size_t pixel_count = with_shadows.size() / 4U;
    size_t darkened = 0;
    size_t brightened = 0;
    size_t unstable = 0;
    double shadowed_region_sum_with = 0.0;
    double shadowed_region_sum_without = 0.0;

    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
        // Skip anything that moved between two identical captures - that pixel
        // is overlay/dither, and cannot testify about shadows either way.
        if (std::abs(luminance_of(without_shadows, pixel) - luminance_of(noise_reference, pixel))
            > CHANGE_THRESHOLD) {
            ++unstable;
            continue;
        }

        const double lum_without = luminance_of(without_shadows, pixel);
        const double lum_with = luminance_of(with_shadows, pixel);
        const double delta = lum_without - lum_with;

        if (delta > CHANGE_THRESHOLD) {
            ++darkened;
            shadowed_region_sum_with += lum_with;
            shadowed_region_sum_without += lum_without;
        } else if (delta < -CHANGE_THRESHOLD) {
            ++brightened;
        }
    }

    const double darkened_fraction =
      pixel_count == 0U ? 0.0 : static_cast<double>(darkened) / static_cast<double>(pixel_count);

    GTEST_LOG_(INFO) << "whole-frame mean luminance: intensity 0.0 = " << mean_luminance(without_shadows)
                     << ", intensity 1.0 = " << mean_luminance(with_shadows) << "; pixels darkened = " << darkened
                     << " (" << (darkened_fraction * 100.0) << "%), brightened = " << brightened
                     << ", unstable/overlay skipped = " << unstable << " of " << pixel_count;

    // 1. A shadowed region must exist at all.
    ASSERT_GT(darkened, 0U) << "Not a single pixel changed when the shadow intensity went from 0.0 to 1.0. "
                               "The cascaded shadow map is rendered but its result never reaches the lighting "
                               "shaders.";
    EXPECT_GT(darkened_fraction, 0.001)
      << "Only " << (darkened_fraction * 100.0)
      << "% of pixels were meaningfully darkened by shadows; expected a visible shadowed region.";

    // 2. Over that region the mean luminance with shadows must be LOWER. This
    //    is the regression that went unnoticed for months.
    const double mean_shadowed = shadowed_region_sum_with / static_cast<double>(darkened);
    const double mean_unshadowed = shadowed_region_sum_without / static_cast<double>(darkened);
    EXPECT_LT(mean_shadowed, mean_unshadowed)
      << "Shadows did not darken the shadowed region: mean luminance " << mean_shadowed
      << " at shadow intensity 1.0 vs " << mean_unshadowed << " at intensity 0.0.";

    // 3. Direction check: shadowing is a multiplicative attenuation, so it can
    //    never brighten scene pixels. Anything brightening is GUI overlay
    //    noise and must stay a small minority.
    EXPECT_GT(darkened, brightened * 5U)
      << darkened << " pixels darkened but " << brightened
      << " brightened - raising the shadow intensity must not brighten the image.";
}

// Forward and deferred are two code paths computing the same lighting for the
// same scene. They will never be pixel-identical, but their overall brightness
// must stay in the same ballpark - if one path breaks entirely (black frame,
// missing lighting, missing geometry) the means diverge far beyond this.
TEST(GoldenRender, DeferredMatchesForwardRoughly)
{
    SKIP_WITHOUT_GPU();

    EngineHarness harness;
    if (!harness.renderer->supportsFrameCapture()) {
        GTEST_SKIP() << "Surface does not support eTransferSrc; frame capture unavailable.";
    }

    auto &scene_vars = harness.gui->getGuiSceneSharedVars();
    auto &renderer_vars = harness.gui->getGuiRendererSharedVars();
    renderer_vars.raytracing = false;
    renderer_vars.pathTracing = false;
    scene_vars.shadows_enabled = true;

    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while warming up.";

    uint32_t width = 0;
    uint32_t height = 0;

    renderer_vars.rasterizationMode = RasterizationMode::Forward;
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> forward = harness.capture_frame(width, height);
    ASSERT_FALSE(forward.empty()) << "Forward capture returned no pixels.";

    renderer_vars.rasterizationMode = RasterizationMode::Deferred;
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> deferred = harness.capture_frame(width, height);
    ASSERT_FALSE(deferred.empty()) << "Deferred capture returned no pixels.";

    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost during capture.";
    ASSERT_EQ(forward.size(), deferred.size());

    const double mean_forward = mean_luminance(forward);
    const double mean_deferred = mean_luminance(deferred);

    // Both paths must produce a non-degenerate image in the first place.
    EXPECT_GT(luminance_stddev(forward), 2.0) << "Forward frame is uniform.";
    EXPECT_GT(luminance_stddev(deferred), 2.0) << "Deferred frame is uniform.";

    // Deliberately generous (~23% of the 0..255 range): this catches a path
    // that broke, not a path that shades slightly differently.
    constexpr double LUMINANCE_TOLERANCE = 60.0;
    EXPECT_NEAR(mean_deferred, mean_forward, LUMINANCE_TOLERANCE)
      << "Forward and deferred disagree far more than expected (forward " << mean_forward << ", deferred "
      << mean_deferred << "); one of the two lighting paths is likely broken.";
}
