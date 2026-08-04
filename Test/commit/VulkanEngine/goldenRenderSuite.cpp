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
#include "EngineLoadWait.hpp"
#include "GoldenMetrics.hpp"
#include "common/host_device_shared_vars.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <vulkan/vulkan.hpp>
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

import kataglyphis.vulkan.camera;
import kataglyphis.vulkan.gui;
import kataglyphis.vulkan.gui_renderer_shared_vars;
import kataglyphis.vulkan.gui_scene_shared_vars;
import kataglyphis.vulkan.renderer;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.scene_config;
import kataglyphis.vulkan.window;

namespace {

using Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars;
using Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode;
using Kataglyphis::Test::GoldenMetrics::Crop;
using Kataglyphis::Test::GoldenMetrics::card_crop;
using Kataglyphis::Test::GoldenMetrics::detail_fraction;
using Kataglyphis::Test::GoldenMetrics::luminance_of;
using Kataglyphis::Test::GoldenMetrics::mean_luminance_in_crop;
using Kataglyphis::Test::GoldenMetrics::panel_free_crop;
using Kataglyphis::Test::GoldenMetrics::swung_fraction;

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
        auto &guiSceneSharedVars = gui->getGuiSceneSharedVars();
        renderer->updateStateDueToUserInput(guiSceneSharedVars);
        renderer->updateUniforms(scene.get(), camera.get(), guiSceneSharedVars);
        renderer->drawFrame(guiSceneSharedVars);
    }

    /// Pumps frames until the asynchronously parsed model is in the scene.
    /// Called by render_frames so every existing test keeps meaning what it
    /// meant when loading was blocking.
    void wait_for_model()
    {
        Kataglyphis::TestSupport::waitForModelLoad(renderer.get(), [this] { render_frame(); });
    }

    void render_frames(int count)
    {
        wait_for_model();
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

    [[nodiscard]] bool supportsFrameCapture() const { return renderer->supportsFrameCapture(); }

    // Selects forward rasterization (as opposed to ray/path tracing), the mode
    // most golden tests measure. Returns the shared-vars reference so a caller
    // that needs to tweak further fields keeps one binding.
    GUIRendererSharedVars &useForwardRaster()
    {
        auto &renderer_vars = gui->getGuiRendererSharedVars();
        renderer_vars.raytracing = false;
        renderer_vars.pathTracing = false;
        renderer_vars.rasterizationMode = RasterizationMode::Forward;
        return renderer_vars;
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

// Forces the scene the engine loads for the lifetime of the object, via the
// KATAGLYPHIS_MODEL_OVERRIDE hook in SceneConfig::getModelFile.
//
// A test must pick its scene for what the scene MEASURES, not for how it
// looks. The shipped debug scene is a dinosaur skeleton - good to open the app
// on, terrible as a shadow caster, because PCF averages 25 taps and the gaps
// between thin bones leave most of them unoccluded. Measured on the same
// renderer: 0.13% of pixels darkened over the skeleton, 6.45% over a solid
// box. Asserting against the skeleton would mean choosing between a threshold
// so low it proves nothing and a test that fails for a correct renderer.
class ScopedModelOverride
{
  public:
    explicit ScopedModelOverride(const char *relative_path) { set(relative_path); }
    ScopedModelOverride(const ScopedModelOverride &) = delete;
    ScopedModelOverride &operator=(const ScopedModelOverride &) = delete;
    ~ScopedModelOverride() { set(""); }

  private:
    static void set(const char *value)
    {
#ifdef _WIN32
        std::ignore = _putenv_s("KATAGLYPHIS_MODEL_OVERRIDE", value);
#else
        if (*value == '\0') {
            std::ignore = unsetenv("KATAGLYPHIS_MODEL_OVERRIDE");
        } else {
            std::ignore = setenv("KATAGLYPHIS_MODEL_OVERRIDE", value, 1);
        }
#endif
    }
};

// A ground plane with a solid 6x6x6 box floating above it - nothing else.
// Generated deliberately for shadow measurement; see the header of the file.
constexpr const char *SHADOW_RIG_MODEL = "Models/ShadowTest/shadow_rig.obj";
// One mesh, two primitives (two materials) - loads as two meshes via the #10
// multi-mesh loader split.
constexpr const char *TWO_PRIMITIVE_MODEL = "Models/GltfTest/two_primitives.gltf";
constexpr const char *MASK_CARD_MODEL = "Models/GltfTest/mask_card.gltf";
// Two materials, each with its own base-colour texture: image 0 is a valid
// base64 payload that is not a decodable PNG/JPG, image 1 is a small valid
// PNG - the device-side half of the 2026-07-23 texture-index-misalignment fix.
constexpr const char *CORRUPT_EMBEDDED_IMAGE_MODEL = "Models/GltfTest/corrupt_embedded_image.gltf";
constexpr const char *UV_TRANSFORM_MODEL = "Models/GltfTest/uv_transform_card.gltf";
// Near-black base colour, emissiveFactor (1,1,1) - glows regardless of light.
constexpr const char *EMISSIVE_CARD_MODEL = "Models/GltfTest/emissive_card.gltf";
// Same near-black base colour and emissiveFactor as EMISSIVE_CARD_MODEL, plus
// KHR_materials_emissive_strength = 4.0 - big enough to overflow an 8-bit
// UNORM G-buffer channel.
constexpr const char *EMISSIVE_STRENGTH_CARD_MODEL = "Models/GltfTest/emissive_strength_card.gltf";
// White base colour, textured, non-emissive - the "lit but not glowing"
// control for EMISSIVE_CARD_MODEL.
constexpr const char *METALLIC_CARD_MODEL = "Models/GltfTest/metallic_card.gltf";

// Counts spdlog::error (or above) messages logged while alive, by attaching a
// callback sink to the default logger for the object's lifetime. This is the
// only way this suite can observe a Vulkan validation error that a driver
// tolerates without losing the device or visibly corrupting the frame -
// debugUtilsMessengerCallback (VulkanDebug.cpp) routes every eError-severity
// validation message through spdlog::error, so "an invalid/destroyed
// acceleration structure descriptor was bound" surfaces here even when the
// resulting pixels happen to look unchanged (observed on the RX 9070 XT: the
// freed TLAS memory was still readable, so a stale-descriptor bug did not
// reliably move a pixel oracle).
class ScopedValidationErrorCounter
{
  public:
    ScopedValidationErrorCounter()
      : sink_(std::make_shared<spdlog::sinks::callback_sink_mt>(
          [this](const spdlog::details::log_msg &msg) {
              if (msg.level >= spdlog::level::err) { ++error_count_; }
          }))
    {
        spdlog::default_logger()->sinks().push_back(sink_);
    }
    ScopedValidationErrorCounter(const ScopedValidationErrorCounter &) = delete;
    ScopedValidationErrorCounter &operator=(const ScopedValidationErrorCounter &) = delete;
    ~ScopedValidationErrorCounter()
    {
        auto &sinks = spdlog::default_logger()->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), sink_), sinks.end());
    }

    int errorCount() const { return error_count_.load(); }

  private:
    std::shared_ptr<spdlog::sinks::callback_sink_mt> sink_;
    std::atomic<int> error_count_{ 0 };
};

// Points KATAGLYPHIS_GPU_TIMING_JSON at a file for the lifetime of the object,
// same shape as ScopedModelOverride above. The renderer reads the variable in
// cleanUp, so it must stay set until the harness is destroyed.
class ScopedGpuTimingJsonPath
{
  public:
    explicit ScopedGpuTimingJsonPath(const std::string &path) { set(path.c_str()); }
    ScopedGpuTimingJsonPath(const ScopedGpuTimingJsonPath &) = delete;
    ScopedGpuTimingJsonPath &operator=(const ScopedGpuTimingJsonPath &) = delete;
    ~ScopedGpuTimingJsonPath() { set(""); }

  private:
    static void set(const char *value)
    {
#ifdef _WIN32
        std::ignore = _putenv_s("KATAGLYPHIS_GPU_TIMING_JSON", value);
#else
        if (*value == '\0') {
            std::ignore = unsetenv("KATAGLYPHIS_GPU_TIMING_JSON");
        } else {
            std::ignore = setenv("KATAGLYPHIS_GPU_TIMING_JSON", value, 1);
        }
#endif
    }
};

// Common preconditions: a Vulkan-capable GLFW plus a surface that lets us copy
// out of a swapchain image at all.
#define SKIP_WITHOUT_GPU()                                                             \
    do {                                                                               \
        if (!glfw_reports_vulkan_support()) {                                          \
            GTEST_SKIP() << "GLFW/Vulkan runtime is unavailable on this system.";       \
        }                                                                              \
    } while (false)

// A GPU may report Vulkan support yet still lack a surface that allows
// copying out of a swapchain image; the frame-capture tests need that too.
#define SKIP_WITHOUT_FRAME_CAPTURE(harness)                                                        \
    do {                                                                                           \
        if (!(harness).supportsFrameCapture()) {                                                   \
            GTEST_SKIP() << "Surface does not support eTransferSrc; frame capture unavailable.";   \
        }                                                                                           \
    } while (false)

} // namespace

// A rendered frame must carry actual image content: not a uniform colour, not
// (nearly) all black. This is the cheapest possible guard against the whole
// render graph silently producing nothing.
TEST(GoldenRender, RendersNonBlankFrame)
{
    SKIP_WITHOUT_GPU();

    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    auto &scene_vars = harness.gui->getGuiSceneSharedVars();
    harness.useForwardRaster();
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
// Not an assertion - an eyeball. Writes the captured frame to a PNG so a
// human (or a Read tool) can look at what the structural tests are measuring.
// This exists because measurement alone twice led to confident wrong calls on
// this exact feature: a shadow baked into the model was reported as cast, and
// a pixel classifier that only ever saw the ImGui overlay produced two
// retracted conclusions. Numbers say how much changed; only a picture says
// whether it is a shadow.
//
// DISABLED_ by default because it writes a file and asserts nothing. Run with
//   --gtest_also_run_disabled_tests --gtest_filter=*DumpsFrameToPng*
TEST(GoldenRender, DISABLED_DumpsFrameToPng)
{
    SKIP_WITHOUT_GPU();

    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    harness.useForwardRaster();
    harness.gui->getGuiSceneSharedVars().shadows_enabled = true;
    // Maximum intensity, matching ShadowsDarkenSomePixels: the two must
    // measure the same configuration or they cannot be compared, and they
    // disagreed once precisely because this defaulted to 0.65 here.
    harness.gui->getGuiSceneSharedVars().cascaded_shadow_intensity = 1.0F;

    harness.render_frames(WARMUP_FRAMES);
    harness.render_frames(SETTLE_FRAMES);

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> frame = harness.capture_frame(width, height);
    ASSERT_FALSE(frame.empty()) << "Capture returned no pixels.";

    const char *out_path = std::getenv("KATAGLYPHIS_FRAME_DUMP");
    const std::string base = (out_path != nullptr) ? out_path : "frame-dump";
    const auto write = [&](const std::string &suffix, const std::vector<uint8_t> &pixels) {
        const std::string path = base + suffix + ".png";
        ASSERT_NE(stbi_write_png(path.c_str(),
                    static_cast<int>(width),
                    static_cast<int>(height),
                    4,
                    pixels.data(),
                    static_cast<int>(width) * 4),
          0)
          << "Failed to write " << path;
        std::cout << "[  INFO ] wrote " << path << " (" << width << "x" << height << ")\n";
    };

    write("-shadows-on", frame);

    // Now repeat ShadowsDarkenSomePixels' sequence EXACTLY - same order (0.0
    // captured before 1.0), same SETTLE_FRAMES, same noise reference - and
    // report this test's own mean of each buffer next to the PNG of that same
    // buffer. The two tests disagree about whether shadow intensity changes
    // the frame at all; running the identical sequence here is what
    // distinguishes "the sequence matters" from "one of the metrics is
    // wrong", and dumping the very bytes the mean was taken over is what
    // distinguishes "the metric is wrong" from "the PNG path is lossy".
    auto &scene_vars = harness.gui->getGuiSceneSharedVars();

    scene_vars.cascaded_shadow_intensity = 0.0F;
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> golden_order_off = harness.capture_frame(width, height);

    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> golden_order_noise = harness.capture_frame(width, height);

    scene_vars.cascaded_shadow_intensity = 1.0F;
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> golden_order_on = harness.capture_frame(width, height);

    ASSERT_EQ(golden_order_off.size(), golden_order_on.size());
    GTEST_LOG_(INFO) << "golden-order means: intensity 0.0 = " << mean_luminance(golden_order_off)
                     << ", noise reference (same 0.0 state) = " << mean_luminance(golden_order_noise)
                     << ", intensity 1.0 = " << mean_luminance(golden_order_on);

    write("-golden-order-off", golden_order_off);
    write("-golden-order-on", golden_order_on);

    // Same pair with PCF reduced to a single tap. The 5x5 kernel averages 25
    // depth comparisons, so a lacy occluder - the debug scene's caster is a
    // dinosaur SKELETON, thin bones with gaps - yields partial occlusion
    // everywhere and never reaches full shadow. One tap makes the term binary:
    // if the shadow goes strong-but-speckled here, the weak shadow is the
    // occluder's geometry, not a defect.
    const int restore_pcf = scene_vars.pcf_radius;
    scene_vars.pcf_radius = 0;
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> single_tap_on = harness.capture_frame(width, height);
    scene_vars.cascaded_shadow_intensity = 0.0F;
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> single_tap_off = harness.capture_frame(width, height);
    scene_vars.pcf_radius = restore_pcf;
    scene_vars.cascaded_shadow_intensity = 1.0F;

    GTEST_LOG_(INFO) << "single-tap PCF means: intensity 1.0 = " << mean_luminance(single_tap_on)
                     << ", intensity 0.0 = " << mean_luminance(single_tap_off);
    write("-singletap-on", single_tap_on);

    std::vector<uint8_t> single_tap_delta(single_tap_on.size(), 255);
    for (size_t i = 0; i + 3 < single_tap_on.size(); i += 4) {
        for (size_t channel = 0; channel < 3; ++channel) {
            const int d = static_cast<int>(single_tap_off[i + channel]) - static_cast<int>(single_tap_on[i + channel]);
            single_tap_delta[i + channel] = static_cast<uint8_t>(255 - std::clamp(d * 4, 0, 255));
        }
    }
    write("-singletap-delta", single_tap_delta);

    // The dump's original order (1.0 captured first, then 0.0) with a settle
    // far longer than SETTLE_FRAMES, so a slow GUI-to-UBO propagation cannot
    // be the explanation for whatever this one shows.
    scene_vars.cascaded_shadow_intensity = 0.0F;
    harness.render_frames(20);
    uint32_t off_width = 0;
    uint32_t off_height = 0;
    const std::vector<uint8_t> unshadowed = harness.capture_frame(off_width, off_height);
    ASSERT_EQ(unshadowed.size(), frame.size());

    GTEST_LOG_(INFO) << "dump-order means: intensity 1.0 (first capture) = " << mean_luminance(frame)
                     << ", intensity 0.0 (after 20 frames) = " << mean_luminance(unshadowed);

    write("-shadows-off", unshadowed);

    std::vector<uint8_t> difference(frame.size(), 255);
    for (size_t i = 0; i + 3 < frame.size(); i += 4) {
        for (size_t channel = 0; channel < 3; ++channel) {
            const int delta = static_cast<int>(unshadowed[i + channel]) - static_cast<int>(frame[i + channel]);
            // 32x gain, inverted: darkening shows up as black on white. The gain is
            // high because the shadow this scene casts is genuinely faint - see
            // the shadow notes in BACKLOG.md before reading anything into it.
            difference[i + channel] = static_cast<uint8_t>(255 - std::clamp(delta * 32, 0, 255));
        }
    }
    write("-shadow-delta", difference);

    // Delta of the golden-order pair too, at the same gain.
    std::vector<uint8_t> golden_delta(golden_order_on.size(), 255);
    for (size_t i = 0; i + 3 < golden_order_on.size(); i += 4) {
        for (size_t channel = 0; channel < 3; ++channel) {
            const int d = static_cast<int>(golden_order_off[i + channel]) - static_cast<int>(golden_order_on[i + channel]);
            golden_delta[i + channel] = static_cast<uint8_t>(255 - std::clamp(d * 32, 0, 255));
        }
    }
    write("-golden-order-delta", golden_delta);
}

// The direct guard for "the shadow map is rendered but nothing samples it",
// which went unnoticed for months, and for the culling bug that replaced it
// (the shadow pass culled its own casters, leaving the depth map at its clear
// value for ~99.8% of sampled texels).
//
// Runs against a purpose-built rig - a solid box over a plane - rather than
// the shipped debug scene. See ScopedModelOverride for why that distinction
// is what makes this test assertable at all.
TEST(GoldenRender, ShadowsDarkenSomePixels)
{
    SKIP_WITHOUT_GPU();

    const ScopedModelOverride use_shadow_rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    auto &scene_vars = harness.gui->getGuiSceneSharedVars();
    harness.useForwardRaster();

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
    // Threshold set from measurement on this rig, not from taste:
    //
    //   correct renderer                 5.41 - 5.44%  (four consecutive runs)
    //   shadow-pass culling reintroduced 2.43%
    //   shadow map never sampled         ~0%
    //
    // 4% separates all three. Note the culling regression does NOT vanish
    // over a closed occluder - back-face culling simply records the slab's far
    // side instead of its near side, two units deeper - which is why the
    // threshold has to sit above a HALVED signal rather than just above zero.
    // Do not lower it to make a failing renderer pass; re-measure both
    // directions if the rig or the camera changes.
    EXPECT_GT(darkened_fraction, 0.04)
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

// The shadow-pass counterpart of FrustumCullingDropsOffscreenMeshesOnly: the
// cascade pass computes drawn/considered caster counts every frame and, until
// now, discarded them. This is a counter assertion, not a pixel oracle - no
// classifier, no tonemap hazard.
TEST(GoldenRender, ShadowCasterStatsAreReportedAndZeroWhenShadowsAreOff)
{
    SKIP_WITHOUT_GPU();

    EngineHarness harness;
    auto &scene_vars = harness.gui->getGuiSceneSharedVars();
    auto &renderer_vars = harness.useForwardRaster();

    scene_vars.shadows_enabled = true;
    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while warming up.";

    ASSERT_GT(renderer_vars.visibility.shadow_casters_total, 0U)
      << "the renderer reported considering no shadow casters at all; "
         "the shadow visibility counters are not being written";
    EXPECT_LE(renderer_vars.visibility.shadow_casters_drawn, renderer_vars.visibility.shadow_casters_total);

    scene_vars.shadows_enabled = false;
    harness.render_frames(SETTLE_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());

    EXPECT_EQ(renderer_vars.visibility.shadow_casters_total, 0U)
      << "shadow caster counters must be zeroed when the shadow pass does not run";
    EXPECT_EQ(renderer_vars.visibility.shadow_casters_drawn, 0U)
      << "shadow caster counters must be zeroed when the shadow pass does not run";
}

// frustum_culling_enabled used to gate only the two raster paths; the shadow
// pass culled against the cascade frusta unconditionally, so the checkbox
// could not explain a shadow caster that silently vanished. This proves the
// switch now reaches the shadow pass too.
//
// The default scene's casters all already sit inside every cascade's fitted
// ortho box, so drawn == considered there regardless of the flag - testing
// against the default scene alone would pass vacuously even without the fix.
// A caster placed far outside the camera's view (and therefore outside the
// union of cascade frusta) is what makes "culling on" vs "culling off"
// observable in the shadow pass at all.
TEST(GoldenRender, DisablingFrustumCullingAlsoDisablesShadowCasterCulling)
{
    SKIP_WITHOUT_GPU();

    EngineHarness harness;
    auto &scene_vars = harness.gui->getGuiSceneSharedVars();
    auto &renderer_vars = harness.useForwardRaster();

    scene_vars.shadows_enabled = true;

    const glm::mat4 far_placement = glm::translate(glm::mat4(1.0F), glm::vec3(5000.0F, 0.0F, 0.0F));
    const std::optional<uint32_t> far_model = harness.renderer->addModel(SHADOW_RIG_MODEL, far_placement);
    ASSERT_TRUE(far_model.has_value()) << "the far-away caster model failed to load";

    renderer_vars.frustum_culling_enabled = false;
    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while warming up.";

    ASSERT_GT(renderer_vars.visibility.shadow_casters_total, 0U)
      << "the renderer reported considering no shadow casters at all; "
         "the shadow visibility counters are not being written";
    EXPECT_EQ(renderer_vars.visibility.shadow_casters_drawn, renderer_vars.visibility.shadow_casters_total)
      << "with frustum culling disabled, every shadow caster must be drawn regardless of the cascade frusta";

    renderer_vars.frustum_culling_enabled = true;
    harness.render_frames(SETTLE_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());

    EXPECT_LE(renderer_vars.visibility.shadow_casters_drawn, renderer_vars.visibility.shadow_casters_total);
}

// The GPU counterpart of CascadedShadowMapUnit.CascadesRespondToLightDirection
// (cascadedShadowMapSuite.cpp:288): that test proves the light direction
// reaches the cascade matrices, this one proves it reaches the pixels.
//
// Reuses ShadowsDarkenSomePixels's oracle - intensity 0 vs 1 at a fixed
// direction, filtered by a same-state noise reference - under two different
// light directions, then compares the two resulting shadow masks. A shadow
// that darkens but never MOVES (baked into a fixed depth map, or a direction
// that never reaches the shadow pass) produces two near-identical masks and
// fails here while still passing ShadowsDarkenSomePixels.
TEST(GoldenRender, ShadowsMoveWhenTheLightRotates)
{
    SKIP_WITHOUT_GPU();

    const ScopedModelOverride use_shadow_rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    auto &scene_vars = harness.gui->getGuiSceneSharedVars();
    harness.useForwardRaster();
    scene_vars.shadows_enabled = true;

    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while warming up.";

    // Direction B is A rotated 51 degrees about the world Y axis, which
    // leaves the Y (downward) component untouched - both directions keep
    // casting onto the ground plane, only where the shadow falls changes.
    //
    // The angle is not arbitrary within the 50-70 degree range the task asked
    // for: on this rig (a wide, thin slab floating over a 60x60 plane, see
    // shadow_rig.obj) the shadow sweeps out of the camera's crop quickly as
    // the direction rotates - a sweep measured on the host RX 9070 XT found
    // the shadowed-pixel area collapses to near-zero (<0.05%) for rotations
    // of +50..+70 degrees and for -55..-60 degrees, while -49..-52 stays
    // comfortably visible (4.1%-13.9% of pixels, against ShadowsDarkenSomePixels's
    // own ~11.2% at 0 degrees) with the two masks still ~99.98% disjoint. -51
    // sits in the middle of that safe band, not on its edge. Do not change
    // this angle without re-running that sweep - see the DumpsFrameToPng
    // caution below for why a plausible-looking angle can silently measure
    // nothing.
    const glm::vec3 direction_a(scene_vars.directional_light_direction[0],
      scene_vars.directional_light_direction[1],
      scene_vars.directional_light_direction[2]);
    const float rotation_radians = glm::radians(-51.0F);
    const glm::vec3 direction_b(glm::rotate(glm::mat4(1.0F), rotation_radians, glm::vec3(0.0F, 1.0F, 0.0F))
                                 * glm::vec4(direction_a, 0.0F));

    const auto set_direction = [&scene_vars](const glm::vec3 &direction) {
        scene_vars.directional_light_direction[0] = direction.x;
        scene_vars.directional_light_direction[1] = direction.y;
        scene_vars.directional_light_direction[2] = direction.z;
    };

    uint32_t width = 0;
    uint32_t height = 0;

    // Direction A: unshadowed, a same-state noise reference, then shadowed.
    set_direction(direction_a);
    scene_vars.cascaded_shadow_intensity = 0.0F;
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> a_unshadowed = harness.capture_frame(width, height);
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> a_noise_reference = harness.capture_frame(width, height);
    scene_vars.cascaded_shadow_intensity = 1.0F;
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> a_shadowed = harness.capture_frame(width, height);

    // Direction B: same three-capture pattern.
    set_direction(direction_b);
    scene_vars.cascaded_shadow_intensity = 0.0F;
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> b_unshadowed = harness.capture_frame(width, height);
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> b_noise_reference = harness.capture_frame(width, height);
    scene_vars.cascaded_shadow_intensity = 1.0F;
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> b_shadowed = harness.capture_frame(width, height);

    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost during capture.";
    ASSERT_FALSE(a_unshadowed.empty()) << "Capture under direction A returned no pixels.";
    ASSERT_EQ(a_unshadowed.size(), a_shadowed.size());
    ASSERT_EQ(a_unshadowed.size(), b_unshadowed.size());
    ASSERT_EQ(a_unshadowed.size(), b_shadowed.size());

    constexpr double CHANGE_THRESHOLD = 4.0;
    const size_t pixel_count = a_unshadowed.size() / 4U;

    // Same per-pixel mask ShadowsDarkenSomePixels builds (stable against the
    // noise reference, and darkened by raising the intensity), restricted to
    // one light direction.
    const auto build_mask = [pixel_count](const std::vector<uint8_t> &unshadowed,
                               const std::vector<uint8_t> &noise_reference,
                               const std::vector<uint8_t> &shadowed) {
        std::vector<bool> mask(pixel_count, false);
        size_t darkened = 0;
        for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
            if (std::abs(luminance_of(unshadowed, pixel) - luminance_of(noise_reference, pixel))
                > CHANGE_THRESHOLD) {
                continue;
            }
            if (luminance_of(unshadowed, pixel) - luminance_of(shadowed, pixel) > CHANGE_THRESHOLD) {
                mask[pixel] = true;
                ++darkened;
            }
        }
        return std::make_pair(mask, darkened);
    };

    const auto [mask_a, darkened_a] = build_mask(a_unshadowed, a_noise_reference, a_shadowed);
    const auto [mask_b, darkened_b] = build_mask(b_unshadowed, b_noise_reference, b_shadowed);

    const double area_a = static_cast<double>(darkened_a) / static_cast<double>(pixel_count);
    const double area_b = static_cast<double>(darkened_b) / static_cast<double>(pixel_count);

    // Same area floor ShadowsDarkenSomePixels uses (:680) - a direction that
    // casts nothing must not be able to pass by trivially agreeing with
    // another empty mask.
    constexpr double AREA_FLOOR = 0.04;
    EXPECT_GT(area_a, AREA_FLOOR) << "Direction A cast a shadow over only " << (area_a * 100.0)
                                  << "% of pixels; expected a visible shadowed region.";
    EXPECT_GT(area_b, AREA_FLOOR) << "Direction B cast a shadow over only " << (area_b * 100.0)
                                  << "% of pixels; expected a visible shadowed region.";

    size_t intersection = 0;
    size_t mask_union = 0;
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
        if (mask_a[pixel] || mask_b[pixel]) {
            ++mask_union;
            if (mask_a[pixel] && mask_b[pixel]) { ++intersection; }
        }
    }
    const size_t symmetric_difference = mask_union - intersection;
    const double moved_fraction =
      mask_union == 0U ? 0.0 : static_cast<double>(symmetric_difference) / static_cast<double>(mask_union);

    GTEST_LOG_(INFO) << "direction A shadowed " << darkened_a << " px (" << (area_a * 100.0)
                     << "%), direction B shadowed " << darkened_b << " px (" << (area_b * 100.0) << "%); mask union "
                     << mask_union << ", symmetric difference " << symmetric_difference << " ("
                     << (moved_fraction * 100.0) << "% of union)";

    // Threshold set from measurement on this rig, not from taste (RX 9070 XT):
    //
    //   correct renderer (-51 degree rotation)                   99.999%
    //   direction pinned in the shadow pass (updateCascades fed a
    //   constant vec3 instead of guiSceneSharedVars, simulating the
    //   direction never reaching the shadow pass)                 0.77%
    //
    // 50% sits far below the correct-renderer figure and far above the
    // direction-insensitive one. Do not lower it to make a failing renderer
    // pass; re-measure both numbers if the rig, rotation angle or camera
    // changes.
    constexpr double MOVED_FRACTION_THRESHOLD = 0.5;
    EXPECT_GT(moved_fraction, MOVED_FRACTION_THRESHOLD)
      << "Only " << (moved_fraction * 100.0)
      << "% of the shadowed-pixel union differs between the two light directions; the shadow does not appear to "
         "move.";
}

// Forward and deferred are two code paths computing the same lighting for the
// same scene. They will never be pixel-identical, but their overall brightness
// must stay in the same ballpark - if one path breaks entirely (black frame,
// missing lighting, missing geometry) the means diverge far beyond this.
TEST(GoldenRender, DeferredMatchesForwardRoughly)
{
    SKIP_WITHOUT_GPU();

    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

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

    // Mean luminance is a WEAK oracle for parity: this test passed for weeks
    // while the deferred capture was not a deferred frame at all (the GUI
    // stomped the mode back to Forward each frame, and the post input
    // descriptor stayed pinned to the forward texture from init) - forward
    // compared against forward, and the means agreed to within noise. Two
    // stronger instruments below.
    //
    // 1. ABSOLUTE per-channel means of each capture, logged: a probe shader
    //    (solid red/green) is visible here, where a relative diff of two
    //    equally-affected captures is blind to it. This is what exposed the
    //    vacuous comparison.
    const auto channel_means = [](const std::vector<uint8_t> &px, const char *label) {
        double r = 0.0;
        double g = 0.0;
        double b = 0.0;
        const size_t n = px.size() / 4;
        for (size_t i = 0; i < px.size(); i += 4) {
            r += px[i];
            g += px[i + 1];
            b += px[i + 2];
        }
        GTEST_LOG_(INFO) << label << " channel means R " << r / n << " G " << g / n << " B " << b / n;
    };
    channel_means(forward, "forward");
    channel_means(deferred, "deferred");

    // 2. Per-pixel absolute difference: separates "the paths genuinely shade
    //    alike" from "the means happen to agree". Measured on this rig with
    //    both paths REALLY rendering: ~0.2 per channel; a single deliberate
    //    shading defect (an extra tonemap in the deferred lighting) pushes it
    //    past 2. Threshold sits between, far from both.
    double abs_diff_sum = 0.0;
    for (size_t i = 0; i < forward.size(); ++i) {
        abs_diff_sum += std::abs(static_cast<int>(forward[i]) - static_cast<int>(deferred[i]));
    }
    const double mean_abs_diff = abs_diff_sum / static_cast<double>(forward.size());
    GTEST_LOG_(INFO) << "deferred-vs-forward mean abs channel diff: " << mean_abs_diff;
    constexpr double MEAN_ABS_DIFF_LIMIT = 1.0;
    EXPECT_LT(mean_abs_diff, MEAN_ABS_DIFF_LIMIT)
      << "Deferred diverges from forward per-pixel; the paths no longer shade alike.";
}

// Env-tunable placement shared by the emissive goldens below: the 1x1 card
// fixtures load at the origin, which - with the default camera at (0,6,26)
// looking down -Z - projects to screen-centre, squarely behind the opaque
// ImGui panel (see MaskCardDiscardsCutoutTexelsVisually). Scaling the card up
// and pushing it right/forward of the origin, exactly as that test does, is
// what puts it in the GUI-free region GoldenMetrics::card_crop isolates.
static glm::mat4 emissive_card_placement()
{
    const auto env_f = [](const char *name, float fallback) {
        const char *value = std::getenv(name);
        return (value != nullptr) ? std::strtof(value, nullptr) : fallback;
    };
    return glm::translate(glm::mat4(1.0F),
             glm::vec3(env_f("MASK_X", 2.5F), env_f("MASK_Y", 4.0F), env_f("MASK_Z", 15.0F)))
           * glm::scale(glm::mat4(1.0F), glm::vec3(env_f("MASK_SCALE", 2.0F)));
}

// Emission is only a real shading contribution if it actually brightens the
// frame - EmissiveIsConsumedByEveryShadingPath (buildIntegritySuite.cpp) only
// checks the shaders reference material.emission as *text*, which would pass
// unchanged through a shader that reads the value and adds zero. Renders the
// near-black-base-colour EMISSIVE_CARD_MODEL and the lit-but-non-emissive
// METALLIC_CARD_MODEL at the same placement over the same shadow-rig backdrop,
// and asserts the emissive card's own footprint is the brighter of the two.
TEST(GoldenRender, EmissiveMaterialBrightensTheFrame)
{
    SKIP_WITHOUT_GPU();

    {
        // Probes frame-capture support once, up front, so a lack of it skips
        // the whole test instead of surfacing as two failed harnesses below -
        // SKIP_WITHOUT_FRAME_CAPTURE relies on `return` reaching this TEST
        // function directly, which only holds at this nesting depth (not
        // inside the per-card lambda further down).
        ScopedModelOverride rig(SHADOW_RIG_MODEL);
        EngineHarness probe;
        SKIP_WITHOUT_FRAME_CAPTURE(probe);
    }

    const glm::mat4 placement = emissive_card_placement();

    // Each card gets its own harness rather than swapping models mid-harness:
    // addModel only ever adds, so a second call would leave both cards in
    // frame and the crop would measure their sum, not either one alone.
    const auto capture_card_mean = [&placement](const char *card_model, const char *label) {
        ScopedModelOverride rig(SHADOW_RIG_MODEL);
        EngineHarness harness;

        harness.useForwardRaster();
        harness.gui->getGuiSceneSharedVars().shadows_enabled = true;
        harness.render_frames(WARMUP_FRAMES);
        if (harness.renderer->hasDeviceLost()) {
            ADD_FAILURE() << "Device lost while warming up (" << label << ").";
            return std::optional<double>();
        }

        const auto added = harness.renderer->addModel(card_model, placement);
        if (!added.has_value()) {
            ADD_FAILURE() << "adding the " << label << " card failed";
            return std::optional<double>();
        }
        harness.render_frames(SETTLE_FRAMES);
        if (harness.renderer->hasDeviceLost()) {
            ADD_FAILURE() << "Device lost after adding the " << label << " card.";
            return std::optional<double>();
        }

        uint32_t width = 0;
        uint32_t height = 0;
        const std::vector<uint8_t> frame = harness.capture_frame(width, height);
        if (frame.empty()) {
            ADD_FAILURE() << label << " card capture returned no pixels.";
            return std::optional<double>();
        }
        return std::optional<double>(mean_luminance_in_crop(frame, width, height, card_crop(width, height)));
    };

    const std::optional<double> emissive_mean = capture_card_mean(EMISSIVE_CARD_MODEL, "emissive");
    ASSERT_TRUE(emissive_mean.has_value());
    const std::optional<double> metallic_mean = capture_card_mean(METALLIC_CARD_MODEL, "metallic");
    ASSERT_TRUE(metallic_mean.has_value());

    GTEST_LOG_(INFO) << "card-region mean luminance: emissive " << *emissive_mean << ", metallic " << *metallic_mean;

    // Threshold set from measurement on this rig (RX 9070 XT), not from taste;
    // re-measure if the rig, placement or camera changes.
    constexpr double MARGIN = 10.0;
    EXPECT_GT(*emissive_mean, *metallic_mean + MARGIN)
      << "The emissive card's own region is not meaningfully brighter than the non-emissive control; "
         "material.emission is likely not reaching this shading path.";
}

// The red/green pair for the GBUFFER_MATERIAL_FORMAT task: with the 8-bit
// UNORM attachment the deferred G-buffer clips a strength-4 emitter's packed
// emission to 1.0 while the forward pass shades it in full float. ACES
// tonemapping compresses highlights hard, so a clipped-to-1.0 emitter and a
// correct 4.0 one do not look dramatically different once captured - this
// is a real but modest divergence, not a blown-out one (see the measured
// numbers below). Same instrument as DeferredMatchesForwardRoughly, run
// instead against a card that actually carries KHR_materials_emissive_strength.
//
// Deliberately the shipped debug scene (dinosaur), not SHADOW_RIG_MODEL: measured
// on this rig, forward vs deferred diverge by ~5.2 mean-abs against the
// shadow rig's sky-dominated framing REGARDLESS of the card (reproduces with
// the card removed too) - some pre-existing forward/deferred discrepancy in
// that backdrop's sky/ambient handling this test is not about. The debug
// scene's own baseline (DeferredMatchesForwardRoughly) measures 0.41.
TEST(GoldenRender, EmissiveStrengthSurvivesTheDeferredGBuffer)
{
    SKIP_WITHOUT_GPU();

    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    auto &renderer_vars = harness.gui->getGuiRendererSharedVars();
    renderer_vars.raytracing = false;
    renderer_vars.pathTracing = false;
    harness.gui->getGuiSceneSharedVars().shadows_enabled = true;

    renderer_vars.rasterizationMode = RasterizationMode::Forward;
    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while warming up.";

    const auto added = harness.renderer->addModel(EMISSIVE_STRENGTH_CARD_MODEL, emissive_card_placement());
    ASSERT_TRUE(added.has_value()) << "adding the emissive-strength card failed";
    harness.render_frames(SETTLE_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> forward = harness.capture_frame(width, height);
    ASSERT_FALSE(forward.empty()) << "Forward capture returned no pixels.";

    renderer_vars.rasterizationMode = RasterizationMode::Deferred;
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> deferred = harness.capture_frame(width, height);
    ASSERT_FALSE(deferred.empty()) << "Deferred capture returned no pixels.";

    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost during capture.";
    ASSERT_EQ(forward.size(), deferred.size());

    double abs_diff_sum = 0.0;
    for (size_t i = 0; i < forward.size(); ++i) {
        abs_diff_sum += std::abs(static_cast<int>(forward[i]) - static_cast<int>(deferred[i]));
    }
    const double mean_abs_diff = abs_diff_sum / static_cast<double>(forward.size());
    GTEST_LOG_(INFO) << "emissive-strength deferred-vs-forward mean abs channel diff: " << mean_abs_diff;

    if (const char *dump = std::getenv("KATAGLYPHIS_EMISSIVE_DUMP")) {
        const std::string base = dump;
        const auto stride = static_cast<int>(width) * 4;
        stbi_write_png((base + "-forward.png").c_str(), int(width), int(height), 4, forward.data(), stride);
        stbi_write_png((base + "-deferred.png").c_str(), int(width), int(height), 4, deferred.data(), stride);
    }

    // Threshold set from measurement on this rig (RX 9070 XT), not from
    // taste: with GBUFFER_MATERIAL_FORMAT at its shipped
    // eR16G16B16A16Sfloat this measures 0.32; reverting it to
    // eR8G8B8A8Unorm (which clamps the packed emissive channel to 1.0)
    // measures 1.18. 0.7 sits cleanly between the two, red-proven by that
    // revert.
    constexpr double MEAN_ABS_DIFF_LIMIT = 0.7;
    EXPECT_LT(mean_abs_diff, MEAN_ABS_DIFF_LIMIT)
      << "Deferred diverges from forward on the emissive-strength card; the G-buffer attachment is likely "
         "clipping a strength>1 emitter again.";
}

// Frustum culling, end to end through the real renderer.
//
// The unit tests in frustumSuite.cpp cover the maths; this covers the wiring,
// which is where culling actually goes wrong - bounds computed in the wrong
// space, planes built from different matrices than the vertex shader uses, or
// a counter that never moves because the feature is not reached at all.
//
// The drawn/considered counters exist for exactly this reason: culling has no
// visual signature when it works, so without them a test can only observe
// that the picture still looks right, which is also true when culling is a
// no-op.
TEST(GoldenRender, FrustumCullingDropsOffscreenMeshesOnly)
{
    SKIP_WITHOUT_GPU();

    EngineHarness harness;
    auto &renderer_vars = harness.useForwardRaster();
    renderer_vars.frustum_culling_enabled = true;

    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while warming up.";

    // The default debug camera frames the scene, so everything is visible.
    const unsigned int considered = renderer_vars.visibility.meshes_total;
    ASSERT_GT(considered, 0U) << "the renderer reported considering no meshes at all; "
                                 "the visibility counters are not being written";
    EXPECT_EQ(renderer_vars.visibility.meshes_drawn, considered)
      << "culling dropped geometry the camera is looking straight at";

    // Move far away along +Z, well past the far plane, so the scene is behind
    // and beyond the camera. Nothing should survive the frustum test.
    harness.camera->set_camera_position(glm::vec3(0.0F, 0.0F, 5000.0F));
    harness.render_frames(SETTLE_FRAMES);

    EXPECT_EQ(renderer_vars.visibility.meshes_total, considered)
      << "the number of meshes considered must not change when the camera moves";
    EXPECT_EQ(renderer_vars.visibility.meshes_drawn, 0U)
      << "a scene entirely outside the view should be culled completely, but "
      << renderer_vars.visibility.meshes_drawn << " mesh(es) were still drawn";

    // Turning culling off must draw everything again from the same viewpoint.
    // This separates "culling works" from "the renderer stopped drawing".
    renderer_vars.frustum_culling_enabled = false;
    harness.render_frames(SETTLE_FRAMES);
    EXPECT_EQ(renderer_vars.visibility.meshes_drawn, considered)
      << "with culling disabled every mesh must be submitted regardless of the camera";

    ASSERT_FALSE(harness.renderer->hasDeviceLost());
}

// A scene with TWO models, which is what makes the per-draw objectIndex
// observable at all.
//
// The fragment shaders read object_description.i[pc_raster.objectIndex]. With
// one model in the scene that index is always 0, so the indexing could be
// entirely broken and nothing would show it - which is exactly the state this
// engine was in while shader.frag carried "for now only one object allowed".
//
// What this asserts, and what it does not: it proves a second model loads,
// reaches the raster path, and contributes pixels. It does NOT isolate the
// index arithmetic - proving that would need two models whose materials
// differ enough to tell apart in a driver-independent way. The layout
// contract is guarded separately in pushConstantSuite.cpp.
TEST(GoldenRender, SecondModelLoadsAndRenders)
{
    SKIP_WITHOUT_GPU();

    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    auto &renderer_vars = harness.useForwardRaster();
    renderer_vars.frustum_culling_enabled = true;

    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while warming up.";

    const unsigned int meshes_with_one_model = renderer_vars.visibility.meshes_total;
    ASSERT_GT(meshes_with_one_model, 0U);

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> before = harness.capture_frame(width, height);
    ASSERT_FALSE(before.empty());

    // The shadow rig is a solid slab over a plane - large, untextured and
    // unmistakable, so it cannot fail to change the frame if it renders.
    // Placed between the camera and the existing scene.
    const glm::mat4 placement = glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 2.0F, 12.0F))
                                * glm::scale(glm::mat4(1.0F), glm::vec3(0.15F));
    const std::optional<uint32_t> second =
      harness.renderer->addModel("Models/ShadowTest/shadow_rig.obj", placement);

    ASSERT_TRUE(second.has_value()) << "the second model failed to load";
    EXPECT_EQ(*second, 1U) << "the second model must be index 1 - this is the objectIndex the raster path pushes";

    harness.render_frames(SETTLE_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost after adding a model.";

    // The raster path must now consider more meshes than before.
    EXPECT_GT(renderer_vars.visibility.meshes_total, meshes_with_one_model)
      << "adding a model did not change how many meshes the raster path considered; "
         "the new model never reached the draw loop";

    const std::vector<uint8_t> after = harness.capture_frame(width, height);
    ASSERT_EQ(after.size(), before.size());

    size_t changed = 0;
    for (size_t pixel = 0; pixel + 3 < after.size(); pixel += 4) {
        if (std::abs(luminance_of(after, pixel / 4) - luminance_of(before, pixel / 4)) > 4.0) { ++changed; }
    }
    EXPECT_GT(changed, 500U) << "the second model loaded and was counted, but changed only " << changed
                             << " pixels - it is being drawn somewhere invisible, or not drawn at all";
}

// The multi-mesh loader split (backlog #10): a two-PRIMITIVE glTF must load as
// two MESHES, not one flattened mesh. This drives the render half end to end -
// uploadParsed slicing the flat arrays into two Mesh, the record loops iterating
// getMeshCount, and the per-mesh object descriptions. The mesh COUNT is the
// framing-independent proof (pixels depend on the camera); red without the
// split: two_primitives flattens to one mesh and meshes_total is 1.
TEST(GoldenRender, MultiPrimitiveGltfLoadsAsMultipleMeshes)
{
    SKIP_WITHOUT_GPU();

    const ScopedModelOverride model_override(TWO_PRIMITIVE_MODEL);

    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    auto &renderer_vars = harness.useForwardRaster();
    renderer_vars.frustum_culling_enabled = false;// count every mesh, framing aside

    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost())
      << "Device lost rendering the two-primitive glTF - the split produced an invalid mesh.";

    EXPECT_EQ(renderer_vars.visibility.meshes_total, 2U)
      << "the two-primitive glTF must build two meshes (one per primitive), not one flattened mesh";

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> frame = harness.capture_frame(width, height);
    ASSERT_FALSE(frame.empty()) << "the two-primitive glTF captured no frame";
}

// The per-pass GPU timings existed only as a number in the GUI header a human
// squints at. KATAGLYPHIS_GPU_TIMING_JSON turns the same measurements into a
// comparable artifact: render, tear down, diff the file between runs. This
// exercises the whole loop - env var, accumulation over real frames, the write
// in cleanUp - and then holds the artifact to its schema.
TEST(GoldenRender, GpuTimingJsonDumpIsWrittenAndSane)
{
    SKIP_WITHOUT_GPU();

    const std::filesystem::path json_path =
      std::filesystem::temp_directory_path() / "kataglyphis-gpu-timing-dump-test.json";
    std::error_code filesystem_error;
    std::filesystem::remove(json_path, filesystem_error);
    ASSERT_FALSE(std::filesystem::exists(json_path, filesystem_error))
      << "Stale dump at " << json_path << " could not be removed; the test would read a lie.";

    const ScopedGpuTimingJsonPath scoped_path(json_path.string());

    {
        EngineHarness harness;
        harness.useForwardRaster();
        harness.gui->getGuiSceneSharedVars().shadows_enabled = true;

        // Enough frames for the averages to mean something. The first
        // swapchain-image-count readbacks measure nothing (fresh query pools
        // are unreadable until their slice was recorded once), which is
        // exactly what frames_measured is expected to reflect.
        harness.render_frames(30);
        ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while rendering.";
    }// harness teardown calls renderer->cleanUp(), which writes the dump

    ASSERT_TRUE(std::filesystem::exists(json_path, filesystem_error))
      << "Renderer teardown did not write " << json_path;

    std::ifstream file(json_path);
    ASSERT_TRUE(file.is_open()) << "Cannot read back " << json_path;
    std::stringstream content;
    content << file.rdbuf();

    // Non-throwing parse (exceptions are disabled project-wide); a failed
    // parse yields the discarded sentinel instead.
    const nlohmann::json dump = nlohmann::json::parse(content.str(), nullptr, false);
    ASSERT_FALSE(dump.is_discarded()) << "GPU timing dump is not valid JSON: " << content.str();

    ASSERT_TRUE(dump.contains("frames_measured")) << dump.dump(2);
    ASSERT_TRUE(dump.contains("timestamps_supported")) << dump.dump(2);
    ASSERT_TRUE(dump.contains("passes")) << dump.dump(2);
    ASSERT_TRUE(dump["passes"].is_object()) << dump.dump(2);

    if (!dump["timestamps_supported"].get<bool>()) {
        // The file existing and parsing was the assertable part on such a
        // device - it is what distinguishes "cannot measure" from "export
        // broken". There are no averages to check.
        EXPECT_EQ(dump["frames_measured"].get<std::uint64_t>(), 0U)
          << "A device without timestamps cannot have measured frames.";
        EXPECT_TRUE(dump["passes"].empty()) << dump.dump(2);
        GTEST_SKIP() << "GPU timestamps unsupported on this device; average checks not possible.";
    }

    EXPECT_GT(dump["frames_measured"].get<std::uint64_t>(), 0U)
      << "Timestamps are supported but no frame produced a valid sample: " << dump.dump(2);

    // Main, Sky and Post are bracketed unconditionally every frame, so their
    // absence means accumulation is broken, not that a feature was disabled.
    for (const char *always_recorded : { "Main", "Sky", "Post" }) {
        EXPECT_TRUE(dump["passes"].contains(always_recorded))
          << "Pass '" << always_recorded << "' is recorded every frame but missing: " << dump.dump(2);
    }

    for (const auto &[pass_name, average_ms] : dump["passes"].items()) {
        ASSERT_TRUE(average_ms.is_number()) << "Pass '" << pass_name << "' is not a number: " << dump.dump(2);
        const double value = average_ms.get<double>();
        EXPECT_TRUE(std::isfinite(value)) << "Pass '" << pass_name << "' average is not finite.";
        EXPECT_GE(value, 0.0) << "Pass '" << pass_name << "' average is negative.";
    }
}

// Path tracing must ACCUMULATE. With a static camera, every frame draws NEW
// random samples (frame index folded into the RNG seed) and folds them into a
// running mean held in a full-float history image. Observable consequences,
// asserted in order of strength:
//   (1) consecutive early captures DIFFER by clearly more than the GUI-overlay
//       baseline (per-frame sample noise moving the mean) - before 2026-07-22
//       the seed had no frame term and every PT frame was bit-identical, so
//       this assertion is the red/green for the whole feature;
//   (2) the frame is not blank;
//   (3) consecutive captures late in the run differ LESS than the early pair
//       (1/N convergence of the running mean).
//
// The FIRST version of this test skipped the camera nudge below and stayed
// green with per-frame sampling disabled: the history had been seeded during
// the async model-load frames, and the running mean "converging" away from
// those stale startup frames mimicked sample accumulation exactly (early 1.61
// -> late 0.18 with a frame-invariant seed). The nudge forces a clean reset
// AFTER the scene has settled so the deltas measure sampling, nothing else -
// and the same probe found the real defect that a model load rebuilt the AS
// without resetting the history (now reset in updateRaytracingDescriptorSets).
TEST(GoldenRender, PathTracingAccumulatesAndConverges)
{
    SKIP_WITHOUT_GPU();

    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);
    if (!harness.renderer->supportsHardwareRaytracing()) {
        GTEST_SKIP() << "Hardware raytracing unsupported; path tracing unavailable.";
    }

    auto &renderer_vars = harness.gui->getGuiRendererSharedVars();
    renderer_vars.raytracing = false;
    renderer_vars.pathTracing = true;
    renderer_vars.rasterizationMode = RasterizationMode::Forward;
    harness.render_frames(WARMUP_FRAMES);

    // Force a history reset now that the scene is fully loaded and settled: a
    // sub-millimetre camera move changes the view matrix, which restarts the
    // running mean from the next frame. Without this the early pair measures
    // the mean healing from pre-load frames, not per-frame sampling.
    harness.camera->set_camera_position(harness.camera->get_camera_position()
                                        + glm::vec3(0.001F, 0.0F, 0.0F));
    harness.render_frame();

    // Instrument choice (both alternatives were tried and MEASURED wrong):
    // whole-frame mean abs diff cannot see PT noise (GUI text noise ~0.18
    // vs ~0.16 - no separation), and a centre crop landed on the ImGui
    // panel's FPS counter, whose changing digits contributed a constant
    // ~0.29 that dwarfed the signal. Amplified per-pixel diff maps showed
    // the panel covers the LEFT ~70% of the frame and the path-traced
    // content with its sample noise lives at the right edge. So: fraction
    // of CHANGED pixels (any channel moving > 2 levels) in the right-edge
    // region, which contains no GUI at all - with a frame-invariant seed
    // that region is bit-identical between frames and the fraction is
    // exactly zero.
    const auto changed_fraction =
      [](const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, uint32_t w, uint32_t h) {
          const uint32_t x0 = (w * 3U) / 4U;
          const uint32_t x1 = (w * 49U) / 50U;
          const uint32_t y0 = h / 20U;
          const uint32_t y1 = (h * 19U) / 20U;
          size_t changed = 0;
          size_t total = 0;
          for (uint32_t y = y0; y < y1; ++y) {
              for (uint32_t x = x0; x < x1; ++x) {
                  const size_t base = (static_cast<size_t>(y) * w + x) * 4U;
                  for (size_t c = 0; c < 3U; ++c) {
                      if (std::abs(static_cast<int>(a[base + c]) - static_cast<int>(b[base + c])) > 2) {
                          ++changed;
                          break;
                      }
                  }
                  ++total;
              }
          }
          return static_cast<double>(changed) / static_cast<double>(total);
      };

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> early_a = harness.capture_frame(width, height);
    const std::vector<uint8_t> early_b = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());
    ASSERT_FALSE(early_a.empty());
    ASSERT_EQ(early_a.size(), early_b.size());

    // Deepen the history, then measure the per-frame movement again.
    harness.render_frames(40);
    const std::vector<uint8_t> late_a = harness.capture_frame(width, height);
    const std::vector<uint8_t> late_b = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());
    ASSERT_EQ(late_a.size(), late_b.size());

    // Noise-quality diagnostic (logged, not asserted): lag-1 vs lag-16
    // autocorrelation of the depth-2 noise field. The PT survey claimed the
    // linear pixel-index seed produces neighbour-correlated noise; MEASURED
    // 2026-07-22: lag-1 = -0.012, lag-16 = +0.015 - both zero, the claim was
    // false (the LCG pre-step + PCG output hash decorrelate adjacent seeds).
    // The log line stays so a future seed change shows its noise character
    // here.
    {
        const uint32_t cx0 = (width * 18U) / 25U, cx1 = (width * 49U) / 50U;
        const uint32_t cy0 = height / 4U, cy1 = (height * 3U) / 4U;
        auto corr = [&](uint32_t lag) {
            double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0; size_t n = 0;
            for (uint32_t y = cy0; y < cy1; ++y)
                for (uint32_t x = cx0; x + lag < cx1; ++x) {
                    const double a = luminance_of(early_a, (size_t)y * width + x)
                                   - luminance_of(early_b, (size_t)y * width + x);
                    const double b = luminance_of(early_a, (size_t)y * width + x + lag)
                                   - luminance_of(early_b, (size_t)y * width + x + lag);
                    sx += a; sy += b; sxx += a * a; syy += b * b; sxy += a * b; ++n;
                }
            const double cov = sxy / n - (sx / n) * (sy / n);
            const double va = sxx / n - (sx / n) * (sx / n);
            const double vb = syy / n - (sy / n) * (sy / n);
            return (va > 0 && vb > 0) ? cov / std::sqrt(va * vb) : 0.0;
        };
        GTEST_LOG_(INFO) << "noise autocorrelation lag-1: " << corr(1) << ", lag-16: " << corr(16);
    }

    const double early_delta = changed_fraction(early_a, early_b, width, height);
    const double late_delta = changed_fraction(late_a, late_b, width, height);
    GTEST_LOG_(INFO) << "consecutive-frame changed fraction (right-edge crop): early = " << early_delta
                     << ", late = " << late_delta;

    // (2) Not blank: the accumulated image reaches the screen at all.
    EXPECT_GT(fraction_above(late_b, 8.0), 0.01)
      << "Path-traced frame is (nearly) black - accumulated image not reaching the screen.";

    // (1) Early frames must move. Measured on the RX 9070 XT: sampling
    // active gives early = 5.2e-4 (~118 changed pixels in the crop at
    // history depth 2/3); the red probe (frame term removed from the seed,
    // the pre-2026-07-22 behaviour) gives EXACTLY 0 - the crop is GUI-free,
    // so frozen sampling means bit-identical pixels. 1e-4 (~23 pixels) sits
    // between with 5x margin on the green side.
    EXPECT_GT(early_delta, 1.0e-4)
      << "Consecutive path-traced frames are (near) identical in the scene region - "
         "per-frame seeding inactive.";

    // (3) The running mean must settle: measured late fraction is exactly 0
    // at depth ~45 (every per-pixel movement is under the 2-level metric
    // threshold). Half the early fraction keeps room for driver variance
    // while still requiring a real collapse of per-frame movement.
    EXPECT_LT(late_delta, early_delta / 2.0)
      << "Per-frame movement did not shrink with accumulation depth - history not converging.";
}

// The primary ray is now jittered uniformly within the pixel (box filter)
// instead of always sampling the exact centre, so accumulation should
// anti-alias geometric silhouette edges: a pixel whose area straddles an
// edge sometimes samples the foreground and sometimes the background, and
// the running mean over many frames blends those into a colour strictly
// between the two flat regions either side. Without jitter every sample
// re-hits the same sub-pixel point, so a boundary pixel is either fully
// foreground or fully background - "hard" transitions only, no partial
// ones - the same fg/bg-vs-partial-pixel distinction the Rust WebGPU
// renderer's MSAA note (BACKLOG.md, "No anti-aliasing anywhere") describes
// for a diagonal edge under multisampling.
//
// Same shadow rig as the other PT goldens: a solid box floating over a
// ground plane against the sky gives clean, flat-shaded regions on both
// sides of a silhouette edge, so a jump in luminance between neighbours is
// unambiguously an edge and not texture/material detail.
TEST(GoldenRender, PathTracingAntiAliasesGeometricEdges)
{
    SKIP_WITHOUT_GPU();

    ScopedModelOverride rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);
    if (!harness.renderer->supportsHardwareRaytracing()) {
        GTEST_SKIP() << "Hardware raytracing unsupported; path tracing unavailable.";
    }

    auto &renderer_vars = harness.gui->getGuiRendererSharedVars();
    renderer_vars.raytracing = false;
    renderer_vars.pathTracing = true;
    renderer_vars.rasterizationMode = RasterizationMode::Forward;
    harness.render_frames(WARMUP_FRAMES);

    // Same history-reset nudge as PathTracingAccumulatesAndConverges: start
    // the running mean fresh from the fully-loaded, settled scene rather
    // than measuring a mean still healing from pre-load frames.
    harness.camera->set_camera_position(harness.camera->get_camera_position()
                                        + glm::vec3(0.001F, 0.0F, 0.0F));
    harness.render_frame();

    // Accumulate deep, same depth as the furnace golden, so per-sample
    // noise is averaged well below the margin the partial-pixel check below
    // uses - a shallow history would itself look "in between" everywhere
    // from noise alone, which would pass this test vacuously.
    harness.render_frames(80);

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> frame = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());
    ASSERT_FALSE(frame.empty());

    // Walk the panel-free crop (same region the other PT goldens measure
    // in) and look at consecutive pixel triples, both along a row and down
    // a column - the rig's box/ground/sky boundaries in this camera framing
    // are mostly near-horizontal lines (box top/bottom, the ground/sky
    // horizon), which only show up as a jump between VERTICAL neighbours,
    // not horizontal ones. A "hard edge" is a big luminance jump between
    // the outer two pixels of a triple; among those, a "partial pixel" is
    // the middle pixel sitting strictly between the two outer ones, with
    // margin on both sides so accumulation noise near either flat region
    // does not count. Counting triples (not raw neighbour diffs) means both
    // the fg and bg reference values come from the SAME local edge, so this
    // does not depend on knowing the rig's absolute colours.
    const Crop crop = panel_free_crop(width, height);
    constexpr double EDGE_JUMP = 40.0;
    constexpr double PARTIAL_MARGIN = 6.0;
    size_t edges_found = 0;
    size_t edges_with_partial_pixel = 0;
    const auto classify_triple = [&](double before, double mid, double after) {
        const double lo = std::min(before, after);
        const double hi = std::max(before, after);
        if (hi - lo <= EDGE_JUMP) { return; }
        ++edges_found;
        if (mid > lo + PARTIAL_MARGIN && mid < hi - PARTIAL_MARGIN) { ++edges_with_partial_pixel; }
    };
    for (uint32_t y = crop.y0; y < crop.y1; ++y) {
        for (uint32_t x = crop.x0 + 1U; x + 1U < crop.x1; ++x) {
            classify_triple(luminance_of(frame, static_cast<size_t>(y) * width + (x - 1U)),
              luminance_of(frame, static_cast<size_t>(y) * width + x),
              luminance_of(frame, static_cast<size_t>(y) * width + (x + 1U)));
        }
    }
    for (uint32_t x = crop.x0; x < crop.x1; ++x) {
        for (uint32_t y = crop.y0 + 1U; y + 1U < crop.y1; ++y) {
            classify_triple(luminance_of(frame, static_cast<size_t>(y - 1U) * width + x),
              luminance_of(frame, static_cast<size_t>(y) * width + x),
              luminance_of(frame, static_cast<size_t>(y + 1U) * width + x));
        }
    }

    ASSERT_GT(edges_found, 0U)
      << "No silhouette edge (a >40-level luminance jump between neighbours) found in the panel-free "
         "crop - the shadow rig's box/ground/sky boundary should always produce one; check the crop or rig.";

    const double partial_fraction =
      static_cast<double>(edges_with_partial_pixel) / static_cast<double>(edges_found);
    GTEST_LOG_(INFO) << "silhouette edges found: " << edges_found
                     << ", with a partial (intermediate-luminance) pixel: " << edges_with_partial_pixel << " ("
                     << (partial_fraction * 100.0) << "%)";

    // Measured on the RX 9070 XT: jittered kernel gives 1012 edges / 20.65%
    // with a partial pixel; reverting to the pixel-centre-only `+
    // float2(0.5)` sample point gives 1264 edges / 0.55% (7 pixels) - noise
    // floor, not signal. The threshold sits with wide margin above the red
    // state and below the green one.
    EXPECT_GT(partial_fraction, 0.15)
      << "Almost none of this frame's silhouette edges have a partial (blended) pixel between the "
         "foreground and background luminance - the primary ray is landing on the exact pixel centre "
         "every sample instead of being jittered across the pixel's area.";
}

// The GUI directional light must exist in path tracing. Before 2026-07-22 the
// PT kernel's only light was an accidental radiance-1 white furnace (the env
// term on miss was commented out), so this test's two captures were identical
// no matter what the light slider did - that is the red state. Now: NEE toward
// the directional light per bounce + a deliberate soft sky on miss, so scene
// luminance in the GUI-free right-edge region must drop hard when the light
// radiance goes to zero. Same crop rationale as the accumulation golden: the
// ImGui panel covers the left ~70% of the frame.
TEST(GoldenRender, PathTracingRespondsToTheDirectionalLight)
{
    SKIP_WITHOUT_GPU();

    // The default skeleton scene is the wrong measurand here (measured:
    // lit-unlit delta 0.057 - the right-edge crop is nearly all sky, which
    // the light does not touch). The shadow rig's ground plane + box give the
    // light a real surface area, same reasoning as ShadowsDarkenSomePixels.
    ScopedModelOverride rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);
    if (!harness.renderer->supportsHardwareRaytracing()) {
        GTEST_SKIP() << "Hardware raytracing unsupported; path tracing unavailable.";
    }

    auto &renderer_vars = harness.gui->getGuiRendererSharedVars();
    auto &scene_vars = harness.gui->getGuiSceneSharedVars();
    renderer_vars.raytracing = false;
    renderer_vars.pathTracing = true;
    renderer_vars.rasterizationMode = RasterizationMode::Forward;
    harness.render_frames(WARMUP_FRAMES);

    // Fraction of pixels in the GUI-free region whose colour moves by more
    // than 5 levels between the two captures. A mean-luminance instrument
    // was tried first and measured 0.29 of a level: the lit ground plane
    // CLAMPS at the UNORM ceiling (186 after tonemap) while the responding
    // band hugs the ImGui panel's right edge, so a mean over a crop that
    // missed half the band saw almost nothing. Counting swung pixels is
    // robust to both. Panel edge measured at x = 0.714w; crop starts at
    // 0.72w.
    // Lit: the default radiance (10). Let the accumulation settle a few
    // frames past the mode switch before capturing.
    harness.render_frames(SETTLE_FRAMES);
    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> lit = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());
    ASSERT_FALSE(lit.empty());

    // Unlit: radiance to zero. The accumulation history resets are keyed on
    // camera/scene changes, not light changes, so render enough frames for
    // the running mean to wash the lit history out (the mean moves by
    // (new-old)/N per frame; 30 frames flips the majority of the weight).
    scene_vars.directional_light_radiance = 0.0F;
    harness.render_frames(30);
    const std::vector<uint8_t> unlit = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());
    ASSERT_EQ(lit.size(), unlit.size());

    const double response = swung_fraction(lit, unlit, width, height, panel_free_crop(width, height));
    // Lit crop luminance is logged for cross-mode comparison: with the NEE
    // term carrying its 1/pi, PT's directly lit surfaces should sit in the
    // same brightness regime as the forward path on the same rig (which
    // divides its diffuse by pi) rather than pi-times above it.
    const auto crop_mean_luminance = [](const std::vector<uint8_t> &rgba, uint32_t w, uint32_t h) {
        const uint32_t x0 = (w * 18U) / 25U;
        const uint32_t x1 = (w * 49U) / 50U;
        const uint32_t y0 = h / 20U;
        const uint32_t y1 = (h * 19U) / 20U;
        double sum = 0.0;
        size_t count = 0;
        for (uint32_t y = y0; y < y1; ++y) {
            for (uint32_t x = x0; x < x1; ++x) {
                sum += luminance_of(rgba, static_cast<size_t>(y) * w + x);
                ++count;
            }
        }
        return sum / static_cast<double>(count);
    };
    GTEST_LOG_(INFO) << "PT lit-vs-unlit swung-pixel fraction (panel-free crop): " << response
                     << ", lit crop mean luminance: " << crop_mean_luminance(lit, width, height);

    // Measured: the NEE kernel swings ~4.5k pixels frame-wide (ground plane
    // going from clamped-bright to sky-only lit, deltas up to 244); the
    // fraction below is the in-crop measurement with margin. The red state
    // (pre-NEE kernel, where the only light was the accidental furnace and
    // the radiance slider reached nothing) leaves only accumulation-depth
    // drift, measured well below the threshold.
    EXPECT_GT(response, 5.0e-3)
      << "Path-traced pixels did not respond to the directional light radiance.";
}

// The path-tracing quality controls must actually reach the kernel. The
// discriminator is the bounce cap: at max_bounces = 1 there is no indirect
// sky light on surfaces at all (a primary hit's bounce ray is never traced),
// so large regions render differently than at 8 - and a quality change also
// RESETS the accumulation history (a running mean over two different
// estimators is biased), so the change is fully visible a few frames later.
// Red state: a kernel with hardcoded loop bounds ignores both sliders and
// the captures differ only by accumulation-depth drift.
TEST(GoldenRender, PathTracingHonorsTheQualityControls)
{
    SKIP_WITHOUT_GPU();

    ScopedModelOverride rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);
    if (!harness.renderer->supportsHardwareRaytracing()) {
        GTEST_SKIP() << "Hardware raytracing unsupported; path tracing unavailable.";
    }

    auto &renderer_vars = harness.gui->getGuiRendererSharedVars();
    renderer_vars.raytracing = false;
    renderer_vars.pathTracing = true;
    renderer_vars.rasterizationMode = RasterizationMode::Forward;
    harness.render_frames(WARMUP_FRAMES + SETTLE_FRAMES);

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> full_quality = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());
    ASSERT_FALSE(full_quality.empty());

    renderer_vars.pathTracingMaxBounces = 1;
    harness.render_frames(10);
    const std::vector<uint8_t> single_bounce = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());
    ASSERT_EQ(full_quality.size(), single_bounce.size());

    const double response =
      swung_fraction(full_quality, single_bounce, width, height, panel_free_crop(width, height));
    GTEST_LOG_(INFO) << "PT bounces 8-vs-1 swung-pixel fraction (panel-free crop): " << response;

    // Threshold measured after the first run; the structural requirement is
    // that removing all indirect light changes a real share of scene pixels.
    EXPECT_GT(response, 5.0e-3)
      << "The bounce-cap slider did not change the path-traced image - "
         "quality push constants not reaching the kernel.";
}

// The GUI directional light must drive FORWARD lighting - diffuse included.
// Before 2026-07-22 every BRDF in Resources/ShadersSlang/common/brdf.slang multiplied the
// light color/intensity into the SPECULAR term only, so diffuse surfaces were
// lit by an implicit radiance-1 white light and the radiance slider barely
// moved the image (the red state: only sparse highlights respond). With the
// fix, radiance 10 vs 0 swings the whole lit scene. Rig + crop rationale as
// in the PT sibling test.
TEST(GoldenRender, ForwardLightingRespondsToTheDirectionalLight)
{
    SKIP_WITHOUT_GPU();

    ScopedModelOverride rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    auto &scene_vars = harness.gui->getGuiSceneSharedVars();
    harness.useForwardRaster();
    harness.render_frames(WARMUP_FRAMES + SETTLE_FRAMES);

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> lit = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());
    ASSERT_FALSE(lit.empty());

    scene_vars.directional_light_radiance = 0.0F;
    harness.render_frames(SETTLE_FRAMES);
    const std::vector<uint8_t> unlit = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());
    ASSERT_EQ(lit.size(), unlit.size());

    const double response = swung_fraction(lit, unlit, width, height, panel_free_crop(width, height));

    // Mean luminance of the crop with the light OFF. This is the assertion
    // that actually separates the defect from the fix: a swung-pixel count
    // does NOT - the specular layer alone (which always saw the light)
    // swings 0.41 of the crop when the radiance drops, so that instrument
    // passed on the light-blind diffuse too. But at radiance zero the whole
    // BRDF must go dark: measured, the fixed shader leaves the crop at 18.9
    // (skybox/ambient bleed), while the light-blind diffuse keeps rendering
    // a fully lit scene at 97.6. The threshold sits between.
    const auto crop_mean_luminance = [](const std::vector<uint8_t> &rgba, uint32_t w, uint32_t h) {
        const uint32_t x0 = (w * 18U) / 25U;
        const uint32_t x1 = (w * 49U) / 50U;
        const uint32_t y0 = h / 20U;
        const uint32_t y1 = (h * 19U) / 20U;
        double sum = 0.0;
        size_t count = 0;
        for (uint32_t y = y0; y < y1; ++y) {
            for (uint32_t x = x0; x < x1; ++x) {
                sum += luminance_of(rgba, static_cast<size_t>(y) * w + x);
                ++count;
            }
        }
        return sum / static_cast<double>(count);
    };
    const double unlit_luma = crop_mean_luminance(unlit, width, height);
    const double lit_luma = crop_mean_luminance(lit, width, height);
    GTEST_LOG_(INFO) << "forward lit-vs-unlit swung-pixel fraction (panel-free crop): " << response
                     << ", lit crop mean luminance: " << lit_luma
                     << ", unlit crop mean luminance: " << unlit_luma;

    EXPECT_LT(unlit_luma, 30.0)
      << "The scene stays lit with the directional light at zero radiance - "
         "the light factors are not reaching the diffuse term.";

    // Secondary: the radiance change must move pixels at all (vacuous alone,
    // see above - kept as a cheap sanity floor).
    EXPECT_GT(response, 0.05)
      << "Forward lighting did not respond to the directional light radiance.";
}

// Moving the model in the GUI must move the TRACED world too. The transform
// path rebuilt object descriptions (shading data) but never touched the TLAS,
// so with RT on, the traced image kept rendering the OLD pose no matter where
// the model went - that is the red state, and since the RT mode is fully
// deterministic, its captures are bit-identical there. Green: the TLAS is
// rebuilt (instance transforms only; BLAS geometry is unchanged) and the
// image follows the move.
TEST(GoldenRender, RaytracedWorldFollowsTheModelTransform)
{
    SKIP_WITHOUT_GPU();

    ScopedModelOverride rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);
    if (!harness.renderer->supportsHardwareRaytracing()) {
        GTEST_SKIP() << "Hardware raytracing unsupported.";
    }

    auto &renderer_vars = harness.gui->getGuiRendererSharedVars();
    auto &scene_vars = harness.gui->getGuiSceneSharedVars();
    renderer_vars.raytracing = true;
    renderer_vars.pathTracing = false;
    renderer_vars.rasterizationMode = RasterizationMode::Forward;
    harness.render_frames(WARMUP_FRAMES + SETTLE_FRAMES);

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> before = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());
    ASSERT_FALSE(before.empty());

    // Raise the model the way the GUI does it.
    scene_vars.selected_model_index = 0;
    scene_vars.model_position[1] += 3.0F;
    scene_vars.model_transform_changed = true;
    harness.render_frames(SETTLE_FRAMES);

    const std::vector<uint8_t> after = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());
    ASSERT_EQ(before.size(), after.size());

    const double moved = swung_fraction(before, after, width, height, panel_free_crop(width, height));
    GTEST_LOG_(INFO) << "RT transform-move swung-pixel fraction (panel-free crop): " << moved;

    // Threshold measured after the first run; the red state is exactly zero
    // (deterministic RT tracing a stale TLAS).
    EXPECT_GT(moved, 5.0e-3)
      << "The traced image did not follow the model transform - stale TLAS.";
}

// The ray-traced closest-hit shader used to seed every pixel's payload with
// its full unlit albedo (`payload.hit_value = ambient`) before adding the
// shadow-tested direct term - an ambient/IBL bounce neither the forward nor
// the deferred lighting path has (both start shading from brdf_direct
// alone). That meant a shadowed ray-traced surface never actually went
// dark: the fake ambient term always showed through at full albedo. Fixed
// by seeding the payload at black instead.
TEST(GoldenRender, RaytracedShadowsAreDarkerThanLitGround)
{
    SKIP_WITHOUT_GPU();

    ScopedModelOverride rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);
    if (!harness.renderer->supportsHardwareRaytracing()) {
        GTEST_SKIP() << "Hardware raytracing unsupported.";
    }

    auto &renderer_vars = harness.gui->getGuiRendererSharedVars();
    renderer_vars.raytracing = true;
    renderer_vars.pathTracing = false;
    renderer_vars.rasterizationMode = RasterizationMode::Forward;
    harness.render_frames(WARMUP_FRAMES);

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> frame = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());
    ASSERT_FALSE(frame.empty());

    // The shadow rig's ground plane spans both the shadowed patch under the
    // floating box and open, directly-lit ground within the same panel-free
    // crop, so a whole-crop mean would average shadow and light together and
    // hide the contrast this golden exists to catch. Instead, compare the
    // mean luminance of the darkest decile of crop pixels (the shadowed
    // patch) against the brightest decile (the lit ground).
    const Crop crop = panel_free_crop(width, height);
    std::vector<double> luminances;
    luminances.reserve(static_cast<size_t>(crop.x1 - crop.x0) * static_cast<size_t>(crop.y1 - crop.y0));
    for (uint32_t y = crop.y0; y < crop.y1; ++y) {
        for (uint32_t x = crop.x0; x < crop.x1; ++x) {
            luminances.push_back(luminance_of(frame, static_cast<size_t>(y) * width + x));
        }
    }
    std::sort(luminances.begin(), luminances.end());
    const size_t decile = std::max<size_t>(1U, luminances.size() / 10U);
    double dark_sum = 0.0;
    for (size_t i = 0; i < decile; ++i) { dark_sum += luminances[i]; }
    double lit_sum = 0.0;
    for (size_t i = luminances.size() - decile; i < luminances.size(); ++i) { lit_sum += luminances[i]; }
    const double dark_mean = dark_sum / static_cast<double>(decile);
    const double lit_mean = lit_sum / static_cast<double>(decile);

    GTEST_LOG_(INFO) << "RT shadow darkest-decile mean luminance: " << dark_mean
                     << ", lit-decile mean luminance: " << lit_mean;

    // MEASURED 0.0 dark vs 244 lit with the fix (ratio 0.0 - shadowed ground
    // has no ambient term left to show, so it comes out exactly black);
    // reverting step 2 in raytrace.rchit.slang (payload.hit_value = albedo
    // instead of float3(0.0)) measures 41.6 dark vs 248 lit (ratio ~0.168) -
    // the shadowed patch still shows most of its unlit albedo. Gate at 0.05,
    // strictly between the two ratios.
    EXPECT_LT(dark_mean, lit_mean * 0.05)
      << "Ray-traced shadow did not go dark relative to the lit ground - the "
         "closest-hit shader is still seeding an ambient/unlit-albedo term.";
}

// The full forward/deferred scene used to be rasterized every RT/PT frame
// into an image the RT dispatch then overwrote completely - dead GPU work.
// record_commands now skips recordRasterPass whenever raytracingOwnsFrame()
// is true. The deterministic proof is the visibility counters, which the
// render loop now zeroes rather than leaving stale: they must read 0 while
// RT owns the frame (a picture that still looks right proves nothing - the
// RT output would look identical whether or not the dead raster work ran
// underneath it).
//
// The smoothed "Main" GPU-timing bucket (raster and RT/PT share it) is
// measured too, but only logged, not asserted as "must drop": measured on
// the RX 9070 XT with the shipped dinosaur mesh, hardware-RT primary-ray
// dispatch over the full framebuffer costs *slightly more* than forward
// rasterizing the same scene (~0.74ms vs ~0.63ms), so "RT must be cheaper
// than raster" is not a safe assumption on this hardware/scene - the timing
// number is a diagnostic, the counters are the oracle.
TEST(GoldenRender, RaytracingFrameSkipsTheRasterPass)
{
    SKIP_WITHOUT_GPU();

    EngineHarness harness;
    if (!harness.renderer->supportsHardwareRaytracing()) {
        GTEST_SKIP() << "Hardware raytracing unsupported.";
    }

    auto &renderer_vars = harness.useForwardRaster();
    renderer_vars.frustum_culling_enabled = true;

    // Render enough frames to flush the rolling GPU-timing window
    // (GpuTimingSubsystem::GpuPassAverage::WINDOW == 30) with forward-only
    // samples before reading a baseline.
    harness.render_frames(WARMUP_FRAMES + 30);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while warming up.";

    const unsigned int forward_meshes_drawn = renderer_vars.visibility.meshes_drawn;
    ASSERT_GT(forward_meshes_drawn, 0U) << "the renderer reported drawing no meshes at all in forward mode";
    const float forward_main_ms =
      renderer_vars.gpuTimings.pass_ms[static_cast<size_t>(Kataglyphis::VulkanRendererInternals::FrontendShared::GpuTimedPass::Main)];

    renderer_vars.raytracing = true;
    // Flush the rolling window again so the average is dominated by RT-only
    // samples rather than a mix carried over from forward mode.
    harness.render_frames(30);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());

    EXPECT_EQ(renderer_vars.visibility.meshes_drawn, 0U)
      << "the raster pass must not run - and must not report stale mesh counts - while RT owns the frame";
    EXPECT_EQ(renderer_vars.visibility.meshes_total, 0U)
      << "the raster pass must not run - and must not report stale mesh counts - while RT owns the frame";

    if (renderer_vars.gpuTimings.supported) {
        const float rt_main_ms =
          renderer_vars.gpuTimings
            .pass_ms[static_cast<size_t>(Kataglyphis::VulkanRendererInternals::FrontendShared::GpuTimedPass::Main)];
        GTEST_LOG_(INFO) << "Main-pass GPU time: forward(raster only)=" << forward_main_ms
                          << "ms, RT(dispatch only)=" << rt_main_ms << "ms";
    }
}

// A SECOND model must shade with its OWN textures. Material textureIDs are
// model-local, but the shared texture array used to hold model 0's textures
// only - an added model's IDs collided with the first model's slots, so the
// dinosaur added below rendered with the rig's 1x1 WHITE default texture:
// bright, but colourless. With per-model texture offsets + the flattened
// array it renders its real (coloured) textures. The oracle is therefore
// COLOUR: the fraction of crop pixels with a real channel spread. Lighting
// is white, the rig is neutral grey, so saturation can only come from the
// second model's textures.
TEST(GoldenRender, SecondModelShadesWithItsOwnTextures)
{
    SKIP_WITHOUT_GPU();

    ScopedModelOverride rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    harness.useForwardRaster();
    harness.render_frames(WARMUP_FRAMES);

    // Sponza, because it is the one bundled model with actual texture FILES
    // (23 map_Kd entries): the first attempt used the dinosaur, whose .mtl
    // ships colours but zero textures, so both the broken and the fixed
    // binding sampled the same white default and the oracle was blind.
    const std::string sponza = sceneConfig::resolveModelPath("Models/crytek-sponza/sponza_triag.obj");
    if (!std::filesystem::exists(sponza)) { GTEST_SKIP() << "textured second model not present"; }

    // Native sponza units span roughly +-1800; 0.01 brings the atrium to
    // ~36 units next to the 60-unit rig, inside the camera frame.
    const auto added = harness.renderer->addModel(
      sponza, glm::scale(glm::mat4(1.0F), glm::vec3(0.01F)));
    ASSERT_TRUE(added.has_value()) << "adding the second model failed";
    harness.render_frames(SETTLE_FRAMES);

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> frame = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());
    ASSERT_FALSE(frame.empty());

    // Oracle rework, from LOOKING at the capture (mm_frame diagnostic): the
    // wall the second model puts in the crop renders its brick texture
    // correctly - but the bricks are near-greyscale, so a colour-spread
    // metric measured 2e-5 on a VISIBLY textured wall. What separates a real
    // texture from the red state's flat 1x1 white is DETAIL: the fraction of
    // crop pixels whose right-hand neighbour differs in luminance by more
    // than 6 levels.
    const double detail = detail_fraction(frame, width, height, card_crop(width, height));
    GTEST_LOG_(INFO) << "second-model texture-detail fraction (panel-free crop): " << detail;

    EXPECT_GT(detail, 0.02)
      << "The added model shows no texture detail - it is sampling the first "
         "model's (flat white) texture slots.";
}

// Sponza alone (33 textures) must fit inside MAX_TEXTURE_COUNT and bind
// without a validation error. This is the direct regression guard for the
// 2026-07-22 deep-dive item #3 fix: the release default scene overflowed the
// old cap of 24, so 9 of Sponza's textures never reached a descriptor.
// Structural, not a pixel oracle - a colour oracle on Sponza was measured
// blind before (its bricks are near-greyscale); the useful assertion here is
// that every texture slot the scene needs actually fits under the cap.
TEST(GoldenRender, SponzaBindsEveryTextureSlot)
{
    SKIP_WITHOUT_GPU();

    const std::string sponza = sceneConfig::resolveModelPath("Models/crytek-sponza/sponza_triag.obj");
    if (!std::filesystem::exists(sponza)) { GTEST_SKIP() << "Sponza model not present"; }

    ScopedModelOverride sponza_override(sponza.c_str());
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    harness.useForwardRaster();
    harness.render_frames(WARMUP_FRAMES);

    EXPECT_LE(harness.scene->getTextureCount(0), static_cast<uint32_t>(MAX_TEXTURE_COUNT))
      << "Sponza needs more texture slots than MAX_TEXTURE_COUNT provides.";

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> frame = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while binding Sponza's textures.";
    ASSERT_FALSE(frame.empty()) << "Frame capture returned no pixels.";
}

// The device-side half of the 2026-07-23 texture-index-misalignment fix
// (uploadParsed fills a failed decode with the default texture instead of
// skipping the slot). corrupt_embedded_image.gltf has two materials: material
// 0's base-colour image decodes to bytes but is not a valid PNG/JPG (so
// Texture::createFromMemory fails), material 1's is a small valid PNG. Before
// the fix, the failed decode skipped `addTexture` entirely, so this model
// would upload only ONE texture and material 1's textureID (1) would index
// past the descriptor array. GltfParseUnit.CorruptEmbeddedImageStillAssigns...
// already pins the CPU half (parseCpu records both images and the dense
// textureIDs); this is the GPU half - the actual model that reaches the
// device must still end up with two texture slots, one per material,
// regardless of the first one's decode failure.
TEST(GoldenRender, CorruptEmbeddedImageKeepsTextureSlotsAligned)
{
    SKIP_WITHOUT_GPU();

    ScopedModelOverride corrupt_override(CORRUPT_EMBEDDED_IMAGE_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    harness.useForwardRaster();
    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost())
      << "Device lost loading a model with a corrupt embedded image - the failed decode was not handled.";

    EXPECT_EQ(harness.scene->getTextureCount(0), 2U)
      << "a skipped slot would shift material 1's textureID down and index past the descriptor array";
}

// glTF alphaMode MASK visually: a cut-out card must DISCARD its below-cutoff
// texels so the scene behind shows through the holes. The forward discard is
// CPU-verified (GltfParseUnit cutoff) and shadow-golden verified, but never
// confirmed in the forward COLOUR image. Differential oracle (needs NO uniform
// background): render the base scene (A), add the card (B), and measure the
// fraction of CHANGED pixels WITHIN the bounding box of the changed pixels.
// mask_card.gltf's texture is a verified 50/50 8x8 checkerboard, so with the
// discard working only the ~half opaque texels change the frame (the holes
// leave the background = A) -> fraction ~0.5 in the box. Remove the discard and
// the solid card covers its whole footprint -> fraction near 1.0. The bounding
// box auto-locates the card, so this needs no hand-tuned crop. Set
// KATAGLYPHIS_MASK_DUMP to a path prefix to dump before/after/diff PNGs.
TEST(GoldenRender, MaskCardDiscardsCutoutTexelsVisually)
{
    SKIP_WITHOUT_GPU();

    ScopedModelOverride rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    harness.useForwardRaster();
    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while warming up.";

    // A: the base scene, before the card is added.
    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> before = harness.capture_frame(width, height);
    ASSERT_FALSE(before.empty());

    // Scale the 1x1 card up and place it in the GUI-free RIGHT third of the
    // frame, in front of the default camera (0,6,26 looking down -Z); its +Z
    // face points at the camera (doubleSided is not honoured yet, so the front
    // must face us). The opaque ImGui panel covers the left ~68% and the card
    // casts a floor shadow at the bottom, so the card must sit clear of both -
    // the measurement below only scans the upper-right region. Placement is
    // env-tunable (MASK_X/Y/Z/SCALE) so framing can be dialled in without a
    // rebuild.
    const auto env_f = [](const char *name, float fallback) {
        const char *value = std::getenv(name);
        return (value != nullptr) ? std::strtof(value, nullptr) : fallback;
    };
    const glm::mat4 placement =
      glm::translate(glm::mat4(1.0F),
        glm::vec3(env_f("MASK_X", 2.5F), env_f("MASK_Y", 4.0F), env_f("MASK_Z", 15.0F)))
      * glm::scale(glm::mat4(1.0F), glm::vec3(env_f("MASK_SCALE", 2.0F)));
    const auto added = harness.renderer->addModel(MASK_CARD_MODEL, placement);
    ASSERT_TRUE(added.has_value()) << "adding the mask card failed";
    harness.render_frames(SETTLE_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());

    // B: with the card.
    uint32_t w2 = 0;
    uint32_t h2 = 0;
    const std::vector<uint8_t> after = harness.capture_frame(w2, h2);
    ASSERT_FALSE(after.empty());
    ASSERT_EQ(width, w2);
    ASSERT_EQ(height, h2);

    const auto changed_at = [&](uint32_t x, uint32_t y) {
        const size_t b = (static_cast<size_t>(y) * width + x) * 4U;
        return std::abs(static_cast<int>(before[b]) - static_cast<int>(after[b])) > 12
               || std::abs(static_cast<int>(before[b + 1U]) - static_cast<int>(after[b + 1U])) > 12
               || std::abs(static_cast<int>(before[b + 2U]) - static_cast<int>(after[b + 2U])) > 12;
    };

    // Only scan the GUI-free upper-right region: x past the ImGui panel's right
    // edge, y above the card's floor shadow. The FPS text (centre) and the
    // shadow (bottom) would otherwise pollute the bounding box.
    const uint32_t scan_x0 = (width * 68U) / 100U;
    const uint32_t scan_y1 = (height * 62U) / 100U;

    uint32_t minx = width;
    uint32_t miny = height;
    uint32_t maxx = 0;
    uint32_t maxy = 0;
    size_t changed_total = 0;
    for (uint32_t y = 0; y < scan_y1; ++y) {
        for (uint32_t x = scan_x0; x < width; ++x) {
            if (changed_at(x, y)) {
                ++changed_total;
                minx = std::min(minx, x);
                maxx = std::max(maxx, x);
                miny = std::min(miny, y);
                maxy = std::max(maxy, y);
            }
        }
    }

    if (const char *dump = std::getenv("KATAGLYPHIS_MASK_DUMP")) {
        const std::string base = dump;
        std::vector<uint8_t> diff(before.size(), 0U);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const size_t b = (static_cast<size_t>(y) * width + x) * 4U;
                const bool in_scan = (x >= scan_x0 && y < scan_y1);
                const uint8_t v = changed_at(x, y) ? 255U : 0U;
                diff[b] = v;
                diff[b + 1U] = in_scan ? v : static_cast<uint8_t>(v / 3U);// dim outside the scan box
                diff[b + 2U] = in_scan ? v : static_cast<uint8_t>(v / 3U);
                diff[b + 3U] = 255U;
            }
        }
        const auto stride = static_cast<int>(width) * 4;
        stbi_write_png((base + "-before.png").c_str(), int(width), int(height), 4, before.data(), stride);
        stbi_write_png((base + "-after.png").c_str(), int(width), int(height), 4, after.data(), stride);
        stbi_write_png((base + "-diff.png").c_str(), int(width), int(height), 4, diff.data(), stride);
    }

    ASSERT_GT(changed_total, 200U)
      << "the card barely changed the upper-right region - not visible there (framing/culling)";

    const double box_area =
      static_cast<double>(maxx - minx + 1U) * static_cast<double>(maxy - miny + 1U);
    const double changed_fraction = static_cast<double>(changed_total) / box_area;
    GTEST_LOG_(INFO) << "mask card: changed " << changed_total << " px in upper-right, bbox [" << minx << ","
                     << miny << ".." << maxx << "," << maxy << "] fraction-in-box " << changed_fraction;

    // Discard ON: the visible card is a checkerboard, so only its opaque squares
    // change the frame (the holes leave the background) - MEASURED 0.37 of the
    // bounding box on the RX 9070 XT. Removing the forward FS `discard` makes the
    // solid card fill its box - MEASURED 0.78 (not 1.0 because some cut-out texels
    // are grey and blend into the grey ground). The 0.55 gate sits cleanly between
    // the two, red-proven by disabling the discard + recompiling the spv.
    EXPECT_LT(changed_fraction, 0.55)
      << "the card footprint changed too fully - cut-out texels are NOT being discarded";
    EXPECT_GT(changed_fraction, 0.20)
      << "the changed pixels are too sparse to be the checkerboard - check framing";
}

// The ray-traced twin of MaskCardDiscardsCutoutTexelsVisually above: same
// rig, same card, same differential/bounding-box oracle, but with RT owning
// the frame. Before this task, raytrace.rgen.slang's primary ray and
// raytrace.rchit.slang's shadow ray both passed RAY_FLAG_FORCE_OPAQUE, and
// ASManager built every BLAS geometry with vk::GeometryFlagBitsKHR::eOpaque -
// three independent ways to force the implementation to skip any-hit
// invocation, so raytrace.rahit.slang existing is not enough on its own; the
// primary ray's flag in particular is the one that determines what the
// visible image shows. Confirmed by dumping before/after/diff PNGs
// (KATAGLYPHIS_MASK_DUMP) at each step: with any one of the three still
// forcing opaque, the diff over the card's footprint is a solid block (no
// checkerboard) - MEASURED changed_fraction 0.4499 of the bounding box on the
// RX 9070 XT with all three at their original (pre-task) values. Dropping
// all three - any-hit wired into the pipeline, eOpaque relaxed to {}, both
// TraceRay calls no longer forcing opaque - turns the diff into the actual
// checkerboard: MEASURED 0.2278, next to the forward path's 0.49. The gate
// sits at 0.35, strictly between the two measurements so it fails on the
// pre-fix 0.4499 (red-proven directly, not by inference from the forward
// golden's unrelated 0.55 threshold).
TEST(GoldenRender, MaskCardDiscardsCutoutTexelsInRaytracing)
{
    SKIP_WITHOUT_GPU();

    ScopedModelOverride rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);
    if (!harness.renderer->supportsHardwareRaytracing()) {
        GTEST_SKIP() << "Hardware raytracing unsupported.";
    }

    auto &renderer_vars = harness.gui->getGuiRendererSharedVars();
    renderer_vars.raytracing = true;
    renderer_vars.pathTracing = false;
    renderer_vars.rasterizationMode = RasterizationMode::Forward;
    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while warming up.";

    // A: the base scene, before the card is added.
    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> before = harness.capture_frame(width, height);
    ASSERT_FALSE(before.empty());

    // Same env-tunable placement as the forward discard golden.
    const auto env_f = [](const char *name, float fallback) {
        const char *value = std::getenv(name);
        return (value != nullptr) ? std::strtof(value, nullptr) : fallback;
    };
    const glm::mat4 placement =
      glm::translate(glm::mat4(1.0F),
        glm::vec3(env_f("MASK_X", 2.5F), env_f("MASK_Y", 4.0F), env_f("MASK_Z", 15.0F)))
      * glm::scale(glm::mat4(1.0F), glm::vec3(env_f("MASK_SCALE", 2.0F)));
    const auto added = harness.renderer->addModel(MASK_CARD_MODEL, placement);
    ASSERT_TRUE(added.has_value()) << "adding the mask card failed";
    harness.render_frames(SETTLE_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());

    // B: with the card.
    uint32_t w2 = 0;
    uint32_t h2 = 0;
    const std::vector<uint8_t> after = harness.capture_frame(w2, h2);
    ASSERT_FALSE(after.empty());
    ASSERT_EQ(width, w2);
    ASSERT_EQ(height, h2);

    const auto changed_at = [&](uint32_t x, uint32_t y) {
        const size_t b = (static_cast<size_t>(y) * width + x) * 4U;
        return std::abs(static_cast<int>(before[b]) - static_cast<int>(after[b])) > 12
               || std::abs(static_cast<int>(before[b + 1U]) - static_cast<int>(after[b + 1U])) > 12
               || std::abs(static_cast<int>(before[b + 2U]) - static_cast<int>(after[b + 2U])) > 12;
    };

    // Same GUI-free upper-right scan region as the forward golden.
    const uint32_t scan_x0 = (width * 68U) / 100U;
    const uint32_t scan_y1 = (height * 62U) / 100U;

    uint32_t minx = width;
    uint32_t miny = height;
    uint32_t maxx = 0;
    uint32_t maxy = 0;
    size_t changed_total = 0;
    for (uint32_t y = 0; y < scan_y1; ++y) {
        for (uint32_t x = scan_x0; x < width; ++x) {
            if (changed_at(x, y)) {
                ++changed_total;
                minx = std::min(minx, x);
                maxx = std::max(maxx, x);
                miny = std::min(miny, y);
                maxy = std::max(maxy, y);
            }
        }
    }

    if (const char *dump = std::getenv("KATAGLYPHIS_MASK_DUMP")) {
        const std::string base = dump;
        std::vector<uint8_t> diff(before.size(), 0U);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const size_t b = (static_cast<size_t>(y) * width + x) * 4U;
                const bool in_scan = (x >= scan_x0 && y < scan_y1);
                const uint8_t v = changed_at(x, y) ? 255U : 0U;
                diff[b] = v;
                diff[b + 1U] = in_scan ? v : static_cast<uint8_t>(v / 3U);
                diff[b + 2U] = in_scan ? v : static_cast<uint8_t>(v / 3U);
                diff[b + 3U] = 255U;
            }
        }
        const auto stride = static_cast<int>(width) * 4;
        stbi_write_png((base + "-before.png").c_str(), int(width), int(height), 4, before.data(), stride);
        stbi_write_png((base + "-after.png").c_str(), int(width), int(height), 4, after.data(), stride);
        stbi_write_png((base + "-diff.png").c_str(), int(width), int(height), 4, diff.data(), stride);
    }

    ASSERT_GT(changed_total, 200U)
      << "the card barely changed the upper-right region - not visible there (framing/culling)";

    const double box_area = static_cast<double>(maxx - minx + 1U) * static_cast<double>(maxy - miny + 1U);
    const double changed_fraction = static_cast<double>(changed_total) / box_area;
    GTEST_LOG_(INFO) << "RT mask card: changed " << changed_total << " px in upper-right, bbox [" << minx << ","
                     << miny << ".." << maxx << "," << maxy << "] fraction-in-box " << changed_fraction;

    EXPECT_LT(changed_fraction, 0.35)
      << "the card footprint changed too fully in RT - cut-out texels are NOT being discarded";
    EXPECT_GT(changed_fraction, 0.15)
      << "the changed pixels are too sparse to be the checkerboard - check framing";
}

// glTF material.doubleSided: a doubleSided mesh must render BOTH faces. Same rig
// as the discard golden but the card is rotated 180 deg about Y so its BACK face
// points at the camera. Single-sided (the pre-doubleSided behaviour) back-face
// culls it -> invisible; doubleSided renders it -> the checkerboard appears. So a
// visible card here proves the doubleSided flag flows glTF -> Mesh -> the raster
// pass's per-draw eNone cull mode. mask_card.gltf is doubleSided in the fixture.
// Red-proven by forcing single-sided (the card vanishes). Env-tunable placement.
TEST(GoldenRender, MaskCardDoubleSidedRendersFromBehind)
{
    SKIP_WITHOUT_GPU();

    ScopedModelOverride rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    harness.useForwardRaster();
    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> before = harness.capture_frame(width, height);
    ASSERT_FALSE(before.empty());

    const auto env_f = [](const char *name, float fallback) {
        const char *value = std::getenv(name);
        return (value != nullptr) ? std::strtof(value, nullptr) : fallback;
    };
    // Same placement as the discard golden, but rotated 180 deg about Y so the
    // card's +Z face points AWAY from the camera (the back face is what we see).
    const glm::mat4 placement =
      glm::translate(glm::mat4(1.0F),
        glm::vec3(env_f("MASK_X", 2.5F), env_f("MASK_Y", 4.0F), env_f("MASK_Z", 15.0F)))
      * glm::rotate(glm::mat4(1.0F), glm::radians(180.0F), glm::vec3(0.0F, 1.0F, 0.0F))
      * glm::scale(glm::mat4(1.0F), glm::vec3(env_f("MASK_SCALE", 2.0F)));
    const auto added = harness.renderer->addModel(MASK_CARD_MODEL, placement);
    ASSERT_TRUE(added.has_value()) << "adding the mask card failed";
    harness.render_frames(SETTLE_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());

    uint32_t w2 = 0;
    uint32_t h2 = 0;
    const std::vector<uint8_t> after = harness.capture_frame(w2, h2);
    ASSERT_FALSE(after.empty());
    ASSERT_EQ(width, w2);
    ASSERT_EQ(height, h2);

    const auto changed_at = [&](uint32_t x, uint32_t y) {
        const size_t b = (static_cast<size_t>(y) * width + x) * 4U;
        return std::abs(static_cast<int>(before[b]) - static_cast<int>(after[b])) > 12
               || std::abs(static_cast<int>(before[b + 1U]) - static_cast<int>(after[b + 1U])) > 12
               || std::abs(static_cast<int>(before[b + 2U]) - static_cast<int>(after[b + 2U])) > 12;
    };

    const uint32_t scan_x0 = (width * 68U) / 100U;
    const uint32_t scan_y1 = (height * 62U) / 100U;
    uint32_t minx = width;
    uint32_t miny = height;
    uint32_t maxx = 0;
    uint32_t maxy = 0;
    size_t changed_total = 0;
    for (uint32_t y = 0; y < scan_y1; ++y) {
        for (uint32_t x = scan_x0; x < width; ++x) {
            if (changed_at(x, y)) {
                ++changed_total;
                minx = std::min(minx, x);
                maxx = std::max(maxx, x);
                miny = std::min(miny, y);
                maxy = std::max(maxy, y);
            }
        }
    }

    // The load-bearing assertion: the back-facing card is VISIBLE. Single-sided
    // culling would leave changed_total ~0 here (nothing new drawn in the region).
    ASSERT_GT(changed_total, 1500U)
      << "the back-facing doubleSided card is not visible - back-face culled, so "
         "the doubleSided flag is not reaching the per-draw cull mode";

    const double box_area =
      static_cast<double>(maxx - minx + 1U) * static_cast<double>(maxy - miny + 1U);
    const double changed_fraction = static_cast<double>(changed_total) / box_area;
    GTEST_LOG_(INFO) << "doubleSided card (from behind): changed " << changed_total << " px, fraction-in-box "
                     << changed_fraction;
    // And it is the cut-out checkerboard, not a solid blob or artifact.
    EXPECT_GT(changed_fraction, 0.20);
    EXPECT_LT(changed_fraction, 0.80);
}

// The deferred twin of the test above: doubleSided must also disable back-face
// culling in the G-buffer (geometry) pass, not just the forward pass. Same
// back-to-camera card; single-sided would cull it out of the G-buffer, so it
// would never reach the lighting pass. Confirms the deferred DeferredRasterizer
// wired the same per-draw eNone cull mode.
TEST(GoldenRender, MaskCardDoubleSidedRendersFromBehindDeferred)
{
    SKIP_WITHOUT_GPU();

    ScopedModelOverride rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    auto &renderer_vars = harness.gui->getGuiRendererSharedVars();
    renderer_vars.raytracing = false;
    renderer_vars.pathTracing = false;
    renderer_vars.rasterizationMode = RasterizationMode::Deferred;
    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> before = harness.capture_frame(width, height);
    ASSERT_FALSE(before.empty());

    const auto env_f = [](const char *name, float fallback) {
        const char *value = std::getenv(name);
        return (value != nullptr) ? std::strtof(value, nullptr) : fallback;
    };
    const glm::mat4 placement =
      glm::translate(glm::mat4(1.0F),
        glm::vec3(env_f("MASK_X", 2.5F), env_f("MASK_Y", 4.0F), env_f("MASK_Z", 15.0F)))
      * glm::rotate(glm::mat4(1.0F), glm::radians(180.0F), glm::vec3(0.0F, 1.0F, 0.0F))
      * glm::scale(glm::mat4(1.0F), glm::vec3(env_f("MASK_SCALE", 2.0F)));
    const auto added = harness.renderer->addModel(MASK_CARD_MODEL, placement);
    ASSERT_TRUE(added.has_value()) << "adding the mask card failed";
    harness.render_frames(SETTLE_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());

    uint32_t w2 = 0;
    uint32_t h2 = 0;
    const std::vector<uint8_t> after = harness.capture_frame(w2, h2);
    ASSERT_FALSE(after.empty());
    ASSERT_EQ(width, w2);
    ASSERT_EQ(height, h2);

    const auto changed_at = [&](uint32_t x, uint32_t y) {
        const size_t b = (static_cast<size_t>(y) * width + x) * 4U;
        return std::abs(static_cast<int>(before[b]) - static_cast<int>(after[b])) > 12
               || std::abs(static_cast<int>(before[b + 1U]) - static_cast<int>(after[b + 1U])) > 12
               || std::abs(static_cast<int>(before[b + 2U]) - static_cast<int>(after[b + 2U])) > 12;
    };

    const uint32_t scan_x0 = (width * 68U) / 100U;
    const uint32_t scan_y1 = (height * 62U) / 100U;
    size_t changed_total = 0;
    for (uint32_t y = 0; y < scan_y1; ++y) {
        for (uint32_t x = scan_x0; x < width; ++x) {
            if (changed_at(x, y)) { ++changed_total; }
        }
    }
    GTEST_LOG_(INFO) << "doubleSided card (deferred, from behind): changed " << changed_total << " px";
    ASSERT_GT(changed_total, 1500U)
      << "the back-facing doubleSided card is not in the deferred G-buffer - the "
         "geometry pass back-face culled it despite doubleSided";
}

// glTF KHR_texture_transform: a scale on the base-colour UV must TILE the texture.
// uv_transform_card.gltf carries scale [4,4] on the same 8x8 checkerboard as the
// mask card, and the engine samples with REPEAT, so with the transform applied the
// card shows a 32x32 checkerboard - far more edges per area than the raw 8x8. The
// oracle is the DETAIL fraction (right-neighbour luminance change) inside the card,
// located by the differential against the base scene (GUI-free upper-right, past
// the panel). Removing transform_uv (the shaders sampling the raw UV) drops it back
// to 8x8 -> a much lower detail fraction: the red proof.
TEST(GoldenRender, KhrTextureTransformTilesTheTexture)
{
    SKIP_WITHOUT_GPU();

    ScopedModelOverride rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    harness.useForwardRaster();
    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> before = harness.capture_frame(width, height);
    ASSERT_FALSE(before.empty());

    const auto env_f = [](const char *name, float fallback) {
        const char *value = std::getenv(name);
        return (value != nullptr) ? std::strtof(value, nullptr) : fallback;
    };
    const glm::mat4 placement =
      glm::translate(glm::mat4(1.0F),
        glm::vec3(env_f("MASK_X", 2.5F), env_f("MASK_Y", 4.0F), env_f("MASK_Z", 15.0F)))
      * glm::scale(glm::mat4(1.0F), glm::vec3(env_f("MASK_SCALE", 2.0F)));
    const auto added = harness.renderer->addModel(UV_TRANSFORM_MODEL, placement);
    ASSERT_TRUE(added.has_value()) << "adding the uv-transform card failed";
    harness.render_frames(SETTLE_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());

    uint32_t w2 = 0;
    uint32_t h2 = 0;
    const std::vector<uint8_t> after = harness.capture_frame(w2, h2);
    ASSERT_FALSE(after.empty());
    ASSERT_EQ(width, w2);
    ASSERT_EQ(height, h2);

    // Locate the card: bounding box of changed pixels in the GUI-free upper-right.
    const auto changed_at = [&](uint32_t x, uint32_t y) {
        const size_t b = (static_cast<size_t>(y) * width + x) * 4U;
        return std::abs(static_cast<int>(before[b]) - static_cast<int>(after[b])) > 12
               || std::abs(static_cast<int>(before[b + 1U]) - static_cast<int>(after[b + 1U])) > 12
               || std::abs(static_cast<int>(before[b + 2U]) - static_cast<int>(after[b + 2U])) > 12;
    };
    const uint32_t scan_x0 = (width * 68U) / 100U;
    const uint32_t scan_y1 = (height * 62U) / 100U;
    uint32_t minx = width;
    uint32_t miny = height;
    uint32_t maxx = 0;
    uint32_t maxy = 0;
    size_t changed_total = 0;
    for (uint32_t y = 0; y < scan_y1; ++y) {
        for (uint32_t x = scan_x0; x < width; ++x) {
            if (changed_at(x, y)) {
                ++changed_total;
                minx = std::min(minx, x);
                maxx = std::max(maxx, x);
                miny = std::min(miny, y);
                maxy = std::max(maxy, y);
            }
        }
    }
    ASSERT_GT(changed_total, 500U) << "the uv-transform card is not visible in the upper-right (framing)";

    // DETAIL: fraction of card-box pixels whose right neighbour differs in
    // luminance by more than 6 levels. A finely-tiled checkerboard is nearly all
    // edges; a coarse (untransformed) one is not.
    const double detail = detail_fraction(after, width, height, Crop{minx, maxx + 1U, miny, maxy + 1U});
    GTEST_LOG_(INFO) << "uv-transform card: box [" << minx << "," << miny << ".." << maxx << "," << maxy
                     << "] detail-fraction " << detail;

    // Tiled 4x (32x32 checkers) is a high-frequency pattern. The untransformed
    // 8x8 card measures far lower - the red proof (remove transform_uv, recompile).
    EXPECT_GT(detail, 0.15)
      << "the card is not finely tiled - KHR_texture_transform scale is not reaching the sampled UV";
}

// A model added at runtime via addModel must appear in PATH TRACING, not only in
// the raster paths. addModel loads the geometry and rebuilds the object-description
// buffer, but RT/PT only see the acceleration structure - so without rebuilding the
// AS the added model is invisible to the tracer (found 2026-07-23: an addModel'd
// card was bit-identical whether traced as solid or cut-out). This renders the base
// scene in PT, adds the opaque uv-transform card in front of the camera, and checks
// its finely-tiled checkerboard actually appears in the card's screen box. Red
// without the addModel AS rebuild: the crop stays background, detail near zero.
TEST(GoldenRender, AddedModelAppearsInPathTracing)
{
    SKIP_WITHOUT_GPU();

    ScopedModelOverride rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);
    if (!harness.renderer->supportsHardwareRaytracing()) {
        GTEST_SKIP() << "Hardware raytracing unsupported; path tracing unavailable.";
    }

    auto &renderer_vars = harness.gui->getGuiRendererSharedVars();
    renderer_vars.raytracing = false;
    renderer_vars.pathTracing = true;
    renderer_vars.rasterizationMode = RasterizationMode::Forward;
    harness.render_frames(WARMUP_FRAMES);

    const auto env_f = [](const char *name, float fallback) {
        const char *value = std::getenv(name);
        return (value != nullptr) ? std::strtof(value, nullptr) : fallback;
    };
    const glm::mat4 placement =
      glm::translate(glm::mat4(1.0F),
        glm::vec3(env_f("MASK_X", 2.5F), env_f("MASK_Y", 4.0F), env_f("MASK_Z", 15.0F)))
      * glm::scale(glm::mat4(1.0F), glm::vec3(env_f("MASK_SCALE", 2.0F)));
    const auto added = harness.renderer->addModel(UV_TRANSFORM_MODEL, placement);
    ASSERT_TRUE(added.has_value()) << "adding the card failed";
    // Deepen the PT history after the AS-rebuild reset so noise does not swamp the
    // checkerboard detail.
    harness.render_frames(SETTLE_FRAMES);
    harness.render_frames(60);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> frame = harness.capture_frame(width, height);
    ASSERT_FALSE(frame.empty());

    if (const char *dump = std::getenv("KATAGLYPHIS_MASK_DUMP")) {
        const std::string base = dump;
        stbi_write_png(
          (base + "-ptadd.png").c_str(), int(width), int(height), 4, frame.data(), static_cast<int>(width) * 4);
    }

    // The card's known screen box for the shared placement (GUI-free upper-right).
    const Crop box{(width * 71U) / 100U, (width * 98U) / 100U, (height * 40U) / 100U, (height * 61U) / 100U};
    const double detail = detail_fraction(frame, width, height, box);
    GTEST_LOG_(INFO) << "added-model PT crop-detail-fraction " << detail;

    // MEASURED: 0.108 with the AS rebuilt (the tiled card's checkerboard fills the
    // crop) vs 0.035 without it (the crop stays background). The 0.07 gate sits
    // cleanly between - red-proven by removing the addModel AS rebuild.
    EXPECT_GT(detail, 0.07)
      << "the runtime-added card is not visible in path tracing - the AS was not rebuilt to include it";
}

// A model reload requested from the GUI (picking a different entry in the
// model combo box) must appear in PATH TRACING, not leave the raytracing
// descriptor set bound to the TLAS that reloadModel's AS rebuild just
// destroyed. handleModelReloadRequest rebuilt the acceleration structure but,
// before refreshAfterSceneChange consolidated the scene-changed paths, never
// called updateAllDescriptorSets - so the raytracing descriptor kept
// referencing the destroyed handle. On the RX 9070 XT this driver tolerates
// the stale handle (the freed memory was still readable) so it did not
// reliably move a pixel oracle - the validation layers catch it every time,
// so that is the primary oracle here; the panel-free-crop swing is a
// secondary sanity check that the reload did not otherwise leave the scene
// empty. Drives the reload through the same GUISceneSharedVars flags the GUI
// combo box sets (selected_model_index + model_reload_requested, the same
// shape :1682-1684 uses for the transform flag).
TEST(GoldenRender, ReloadedModelIsVisibleInPathTracing)
{
    SKIP_WITHOUT_GPU();

    // A small, known-good-in-PT scene (the same one AddedModelAppearsInPathTracing
    // starts from) rather than the shipped debug scene - the large dinosaur mesh's
    // BLAS is not what this test measures.
    ScopedModelOverride rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);
    if (!harness.renderer->supportsHardwareRaytracing()) {
        GTEST_SKIP() << "Hardware raytracing unsupported; path tracing unavailable.";
    }

    auto &renderer_vars = harness.gui->getGuiRendererSharedVars();
    auto &scene_vars = harness.gui->getGuiSceneSharedVars();
    renderer_vars.raytracing = false;
    renderer_vars.pathTracing = true;
    renderer_vars.rasterizationMode = RasterizationMode::Forward;
    harness.render_frames(WARMUP_FRAMES + SETTLE_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> before = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());
    ASSERT_FALSE(before.empty());

    // Reload to a different scene (the uv-transform card) through the same
    // flags the GUI's model combo box sets.
    const auto model_paths = sceneConfig::getAvailableModelPaths();
    int reload_index = -1;
    for (size_t i = 0; i < model_paths.size(); ++i) {
        if (std::filesystem::path(model_paths[i]) == std::filesystem::path(UV_TRANSFORM_MODEL)) {
            reload_index = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(reload_index, 0) << UV_TRANSFORM_MODEL << " not found among the scanned models";

    ScopedValidationErrorCounter validation_errors;
    scene_vars.selected_model_index = reload_index;
    scene_vars.model_reload_requested = true;
    harness.render_frames(SETTLE_FRAMES);
    // Deepen the PT history after the AS-rebuild reset so noise does not
    // swamp the measurement (same reasoning as AddedModelAppearsInPathTracing).
    harness.render_frames(60);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());

    EXPECT_EQ(validation_errors.errorCount(), 0)
      << "the reload logged a Vulkan validation error - almost certainly the "
         "raytracing descriptor set still referencing the TLAS the AS rebuild destroyed";

    const std::vector<uint8_t> after = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());
    ASSERT_EQ(before.size(), after.size());

    const double swung = swung_fraction(before, after, width, height, panel_free_crop(width, height));
    GTEST_LOG_(INFO) << "reload PT before/after swung-pixel fraction (panel-free crop): " << swung;

    EXPECT_GT(swung, 0.05)
      << "the reloaded model is not visible in path tracing - the scene "
         "looks unchanged after the reload";
}

// The white furnace: with a uniform environment and albedo forced to 1 (the
// KATAGLYPHIS_PT_FURNACE debug mode), an unbiased estimator converges every
// pixel to EXACTLY the environment radiance, whatever the geometry - the
// geometry becomes invisible. This is the strongest single check on the
// whole estimator chain: cosine-sampling cancellation, Russian-roulette
// reweighting, the accumulation mean, and the tonemap all sit under one
// number.
//
// The ideal value below was RECALIBRATED again (2026-08-04, sRGB-encoding
// the swapchain): the C++ engine's swapchain is UNORM, not sRGB
// (SwapchainChoices.hpp's chooseBestSurfaceFormat), so there is no hardware
// encode on present - post.slang now applies linear_to_srgb itself, exactly
// like the Rust renderer's non-sRGB-canvas path always has. So tonemap(1.0)
// = linear_to_srgb(aces_tonemap(1.0)) * 255 = linear_to_srgb(0.80380) * 255
// ~= 231.60, matching the measured mean below almost exactly. The move from
// 204.97 to 231.60 is the encode being ADDED, not the tonemap changing
// again (that recalibration, 2026-07-31, is preserved below for context).
//
// [2026-07-31, restoring the bounce loop lost in the Slang port]: the
// post-processing tonemap moved from Reinhard+manual-gamma to the ACES
// filmic curve (common/aces.slang, Narkowicz 2015) in the same migration
// that dropped the bounce loop. The OLD "186" band was Reinhard-specific
// and went stale the moment the tonemap changed; it does not indicate a
// bounce-loop bug (cross-checked against a pure-background, single-bounce
// furnace capture, which hits this exact formula with zero escape/bounce
// ambiguity).
TEST(GoldenRender, PathTracingPassesTheWhiteFurnaceTest)
{
    SKIP_WITHOUT_GPU();

    // Same env-scoping pattern as ScopedModelOverride.
    struct ScopedFurnace
    {
        ScopedFurnace() { set("1.0"); }
        ~ScopedFurnace() { set(""); }
        // _putenv_s is Windows-only; the Linux clang lane broke on the raw call.
        // Match ScopedModelOverride's portable split.
        static void set(const char *value)
        {
#ifdef _WIN32
            std::ignore = _putenv_s("KATAGLYPHIS_PT_FURNACE", value);
#else
            if (*value == '\0') {
                std::ignore = unsetenv("KATAGLYPHIS_PT_FURNACE");
            } else {
                std::ignore = setenv("KATAGLYPHIS_PT_FURNACE", value, 1);
            }
#endif
        }
    } furnace;

    ScopedModelOverride rig(SHADOW_RIG_MODEL);
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);
    if (!harness.renderer->supportsHardwareRaytracing()) {
        GTEST_SKIP() << "Hardware raytracing unsupported; path tracing unavailable.";
    }

    auto &renderer_vars = harness.gui->getGuiRendererSharedVars();
    auto &scene_vars = harness.gui->getGuiSceneSharedVars();
    renderer_vars.raytracing = false;
    renderer_vars.pathTracing = true;
    renderer_vars.rasterizationMode = RasterizationMode::Forward;
    // The directional light must not contribute (NEE would add energy on top
    // of the furnace); bounces high so truncation loss stays small.
    scene_vars.directional_light_radiance = 0.0F;
    renderer_vars.pathTracingMaxBounces = 16;
    harness.render_frames(WARMUP_FRAMES);

    // Accumulate deep so per-frame noise is averaged well below the band.
    harness.render_frames(80);

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> frame = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost());
    ASSERT_FALSE(frame.empty());

    const uint32_t x0 = (width * 18U) / 25U;
    const uint32_t x1 = (width * 49U) / 50U;
    const uint32_t y0 = height / 20U;
    const uint32_t y1 = (height * 19U) / 20U;
    double sum = 0.0;
    size_t within = 0;
    size_t total = 0;
    for (uint32_t y = y0; y < y1; ++y) {
        for (uint32_t x = x0; x < x1; ++x) {
            const double lum = luminance_of(frame, static_cast<size_t>(y) * width + x);
            sum += lum;
            if (std::abs(lum - 231.6) <= 6.0) { ++within; }
            ++total;
        }
    }
    const double mean = sum / static_cast<double>(total);
    const double uniform_fraction = static_cast<double>(within) / static_cast<double>(total);
    GTEST_LOG_(INFO) << "furnace crop mean luminance: " << mean
                     << " (ideal 231.6), fraction within +-6: " << uniform_fraction;

    EXPECT_GT(mean, 225.6) << "Furnace converges LOW - the estimator is losing energy "
                              "(beyond the known bounce-cap truncation).";
    EXPECT_LT(mean, 237.6) << "Furnace converges HIGH - the estimator is gaining energy.";
    EXPECT_GT(uniform_fraction, 0.98)
      << "The furnace image is not uniform - geometry is visible, so some path "
         "class is biased.";
}

namespace {
// Small deterministic PRNG (SplitMix-style) so the sweep is reproducible across
// runs without pulling in <random>. A fixed seed means a failing iteration can
// be re-hit exactly.
struct SweepRng
{
    uint64_t state;
    uint32_t next()
    {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<uint32_t>(state >> 32U);
    }
    float uf(float lo, float hi) { return lo + (hi - lo) * (static_cast<float>(next()) / 4294967296.0F); }
    int ui(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo + 1)); }
    bool ub() { return (next() & 1U) != 0U; }
};
}// namespace

// Fuzz the GUI itself: every control the user can touch, driven across its
// ALLOWED range (the ImGui slider/combo limits in GUI.cpp) and in random
// combinations, must render without losing the device or yielding an empty
// frame. This is the guard for the controls no other golden test sets - all ten
// cloud parameters, the light direction/colour, the shadow cascade count /
// resolution, the path-tracing sample/bounce counts - and, crucially, for their
// COMBINATIONS (clouds + 8 cascades + path tracing at once, etc.). It does not
// assert on pixels (every state renders something different); it asserts that no
// possible GUI selection can crash, lose the device, or trip a validation error
// (the debug build runs with validation/ASan, so those surface as failures here).
TEST(GoldenRender, GuiInputSweepNeverCrashesOrLosesTheDevice)
{
    SKIP_WITHOUT_GPU();

    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);
    const bool rt_supported = harness.renderer->supportsHardwareRaytracing();

    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while warming up.";

    auto &s = harness.gui->getGuiSceneSharedVars();
    auto &r = harness.gui->getGuiRendererSharedVars();

    // Heaviest deterministic state first: every control at its resource-hungriest
    // extreme AT ONCE - 4096 shadow maps x 8 cascades + clouds at 128 march steps
    // + path tracing at 64 spp. Uniform random sampling below almost never hits
    // this all-maximum corner, yet it is the state most likely to exhaust GPU
    // memory or time out, so it is worth pinning explicitly.
    if (rt_supported) {
        r.pathTracing = true;
        r.raytracing = false;
    }
    r.rasterizationMode = RasterizationMode::Deferred;
    r.pathTracingSamplesPerPixel = 64;
    r.pathTracingMaxBounces = 16;
    r.frustum_culling_enabled = false;
    s.skybox_enabled = true;
    s.directional_light_radiance = 50.0F;
    s.shadows_enabled = true;
    s.num_shadow_cascades = 8;
    s.shadow_map_res_index = 3;// 4096
    s.pcf_radius = MAX_PCF_RADIUS;
    s.cascaded_shadow_intensity = 1.0F;
    s.shadow_distance = 200.0F;
    s.shadow_resolution_changed = true;
    s.clouds_enabled = true;
    s.cloud_num_march_steps = 128;
    s.cloud_num_march_steps_to_light = 128;
    s.cloud_density_multiplier = 1.0F;
    s.cloud_coverage_threshold = 1.0F;
    s.cloud_pillowness = 1.0F;
    s.cloud_cirrus_effect = 1.0F;
    s.cloud_powder_effect = true;
    harness.render_frames(SETTLE_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost on the all-maximum GUI state";
    {
        uint32_t bw = 0;
        uint32_t bh = 0;
        const std::vector<uint8_t> heavy_frame = harness.capture_frame(bw, bh);
        ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost capturing the all-maximum GUI state";
        ASSERT_FALSE(heavy_frame.empty()) << "Empty frame on the all-maximum GUI state";
    }

    SweepRng rng{ 0x9E3779B97F4A7C15ULL };// fixed seed -> deterministic sweep

    constexpr int SWEEP_ITERATIONS = 16;
    for (int iter = 0; iter < SWEEP_ITERATIONS; ++iter) {
        // Render mode: one of forward / deferred / ray tracing / path tracing,
        // mutually exclusive, as the engine actually drives them. RT/PT only when
        // the device supports hardware ray tracing.
        const int mode = rng.ui(0, rt_supported ? 3 : 1);
        r.raytracing = (mode == 2);
        r.pathTracing = (mode == 3);
        r.rasterizationMode = (mode == 1) ? RasterizationMode::Deferred : RasterizationMode::Forward;
        r.frustum_culling_enabled = rng.ub();
        r.pathTracingSamplesPerPixel = rng.ui(1, 64);
        r.pathTracingMaxBounces = rng.ui(1, 16);

        s.skybox_enabled = rng.ub();
        s.directional_light_radiance = rng.uf(0.0F, 50.0F);
        for (float &c : s.directional_light_color) { c = rng.uf(0.0F, 1.0F); }
        for (float &d : s.directional_light_direction) { d = rng.uf(-1.0F, 1.0F); }

        s.shadows_enabled = rng.ub();
        s.num_shadow_cascades = rng.ui(1, 8);// slider allows 1..8 (clamped to MAX_CASCADES in use)
        s.shadow_map_res_index = rng.ui(0, 3);// combo: 512 / 1024 / 2048 / 4096
        s.pcf_radius = rng.ui(1, MAX_PCF_RADIUS);
        s.cascaded_shadow_intensity = rng.uf(0.0F, 1.0F);
        s.shadow_distance = rng.uf(1.0F, 200.0F);
        // Force the shadow map to honour the new cascade count / resolution.
        s.shadow_resolution_changed = true;

        s.clouds_enabled = rng.ub();
        s.cloud_num_march_steps = rng.ui(1, 128);
        s.cloud_num_march_steps_to_light = rng.ui(1, 128);
        s.cloud_density_multiplier = rng.uf(0.0F, 1.0F);
        s.cloud_coverage_threshold = rng.uf(0.0F, 1.0F);
        s.cloud_pillowness = rng.uf(0.0F, 1.0F);
        s.cloud_cirrus_effect = rng.uf(0.0F, 1.0F);
        s.cloud_powder_effect = rng.ub();

        const std::string desc = "iter " + std::to_string(iter) + ": mode=" + std::to_string(mode)
                                 + " shadows=" + std::to_string(static_cast<int>(s.shadows_enabled))
                                 + " cascades=" + std::to_string(s.num_shadow_cascades)
                                 + " res=" + std::to_string(s.shadow_map_res_index)
                                 + " clouds=" + std::to_string(static_cast<int>(s.clouds_enabled))
                                 + " radiance=" + std::to_string(s.directional_light_radiance);

        // render_frame() applies the GUI state (updateStateDueToUserInput) each
        // frame; a few frames let a shadow-map re-init settle.
        harness.render_frames(SETTLE_FRAMES);
        ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while rendering " << desc;

        uint32_t width = 0;
        uint32_t height = 0;
        const std::vector<uint8_t> frame = harness.capture_frame(width, height);
        ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while capturing " << desc;
        ASSERT_FALSE(frame.empty()) << "Empty frame for " << desc;
    }
}

// Swapchain recreation (a resize, or a lost surface) tears down and rebuilds the
// swapchain, its framebuffers, and every sync object (FrameSync), then must keep
// rendering the same scene. Nothing else in the suite exercises this path - the
// tests otherwise render at a fixed size and never recreate - so a recreation
// bug (dangling fence, stale framebuffer, mis-sized sync arrays) would pass every
// other test. This drives recreateSwapChain() directly and asserts the frame
// after recreation still shows the scene (not a black/garbage frame or a lost
// device). It also covers the FrameSync create-after-cleanUp path.
TEST(GoldenRender, SwapchainRecreationKeepsRendering)
{
    SKIP_WITHOUT_GPU();

    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    harness.useForwardRaster();

    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while warming up.";

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> before = harness.capture_frame(width, height);
    ASSERT_FALSE(before.empty());
    const double before_luma = mean_luminance(before);

    // Rebuild every swapchain / framebuffer / sync resource. The harness window
    // does not resize, so recreation happens at the same size and must yield an
    // equivalent frame.
    harness.renderer->recreateSwapChain();
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost during swapchain recreation.";

    harness.render_frames(SETTLE_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost after swapchain recreation.";

    uint32_t width2 = 0;
    uint32_t height2 = 0;
    const std::vector<uint8_t> after = harness.capture_frame(width2, height2);
    ASSERT_FALSE(after.empty());
    EXPECT_EQ(width, width2);
    EXPECT_EQ(height, height2);

    // Same scene, same camera, same size: the recreated frame must match the
    // pre-recreation frame in overall brightness. A black/garbage frame or a
    // half-torn swapchain would swing the mean luminance far beyond this
    // (generous) tolerance, which absorbs only the overlay/frame jitter.
    const double after_luma = mean_luminance(after);
    EXPECT_NEAR(after_luma, before_luma, 20.0)
      << "Mean luminance changed too much across swapchain recreation (before=" << before_luma
      << ", after=" << after_luma << ") - the recreated swapchain is not rendering the same scene.";
}

// Cross-frame WAR guard for cloudOutputTexture (VulkanRenderer.cpp, the
// clouds block): the image is a SINGLE allocation, not duplicated per
// frame-in-flight (Clouds::recreateFrameResources allocates exactly one), so
// this frame's compute write must be ordered against the previous frame's
// post-pass read of the same image even though the frame-in-flight fence
// does not guarantee that ordering on its own (MAX_FRAME_DRAWS == 3,
// common/Globals.hpp). A stale ordering here would show up as a driver hang
// or a validate_sync SYNC-HAZARD, not a wrong pixel - see
// docs/gpu-golden-testing.md and run Run-SyncValidation.ps1 with clouds
// enabled to check for hazards; this test exists to give that barrier a
// frames-in-flight-crossing exercise in a mode
// GuiInputSweepNeverCrashesOrLosesTheDevice cannot reach today, because its
// own clouds_enabled=true case is paired with path tracing and hits the
// unrelated PT device-lost bug tracked in BACKLOG.md before the sweep gets
// this far.
TEST(GoldenRender, CloudsAcrossManyFramesDoesNotLoseTheDevice)
{
    SKIP_WITHOUT_GPU();

    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);

    harness.useForwardRaster();
    auto &scene_vars = harness.gui->getGuiSceneSharedVars();
    scene_vars.clouds_enabled = true;

    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while warming up with clouds enabled.";

    // Render well past MAX_FRAME_DRAWS frame-in-flight cycles so a cross-frame
    // WAR hazard on cloudOutputTexture has more than one chance to surface.
    constexpr int CLOUD_FRAMES = 30;
    harness.render_frames(CLOUD_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost())
      << "Device lost rendering repeated frames with clouds enabled.";

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> frame = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost during capture.";
    ASSERT_FALSE(frame.empty()) << "Frame capture returned no pixels.";
}

// RT-pipeline large-mesh diagnostic for the "Localize (and fix if cheap) the
// path-tracing compute VK_ERROR_DEVICE_LOST" backlog entry. Every existing
// RT/PT golden that reads v0.position/v0.normal via the vertex
// buffer-device-address path (RaytracedWorldFollowsTheModelTransform above,
// and the PT tests) uses the tiny shadow_rig mesh; none has ever run
// raytrace.rchit.slang's identical read pattern against the
// ~hundreds-of-thousands-vertex dinosaur mesh that
// GoldenRender.PathTracingAccumulatesAndConverges device-loses on. This is a
// diagnostic, not a fix: whichever way it goes narrows where the bug lives -
// a PASS here scopes it to path_tracing.slang/RayQuery specifically, a
// device-lost here scopes it to the shared vertex-upload/BDA path instead
// (ASManager / loader upload / a stride mismatch). Kept LAST in file order on
// purpose: a hard device-lost abort here would otherwise kill every test
// after it in the same process.
TEST(GoldenRender, RaytracedLargeMeshDoesNotLoseTheDevice)
{
    SKIP_WITHOUT_GPU();

    // Deliberately no ScopedModelOverride: the shipped debug scene IS the
    // large dinosaur mesh (Models/Dinosaurs/dinosaurs.obj), the same one
    // PathTracingAccumulatesAndConverges device-loses on.
    EngineHarness harness;
    SKIP_WITHOUT_FRAME_CAPTURE(harness);
    if (!harness.renderer->supportsHardwareRaytracing()) {
        GTEST_SKIP() << "Hardware raytracing unsupported.";
    }

    auto &renderer_vars = harness.gui->getGuiRendererSharedVars();
    renderer_vars.raytracing = true;
    renderer_vars.pathTracing = false;
    renderer_vars.rasterizationMode = RasterizationMode::Forward;
    harness.render_frames(WARMUP_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost while warming up.";

    // The PT reproducer device-loses within 0-2 frames; render well past that
    // window before trusting a pass.
    constexpr int DIAGNOSTIC_FRAMES = 10;
    harness.render_frames(DIAGNOSTIC_FRAMES);
    ASSERT_FALSE(harness.renderer->hasDeviceLost())
      << "RT pipeline device-lost on the large dinosaur mesh - the bug is in the shared "
         "vertex-upload/BDA path, not path_tracing.slang/RayQuery specifically.";

    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t> frame = harness.capture_frame(width, height);
    ASSERT_FALSE(harness.renderer->hasDeviceLost()) << "Device lost during capture.";
    ASSERT_FALSE(frame.empty()) << "Frame capture returned no pixels.";
    EXPECT_GT(fraction_above(frame, 8.0), 0.05)
      << "Captured frame looks blank - the large-mesh RT diagnostic needs a real image to "
         "trust the no-device-lost result.";
}
