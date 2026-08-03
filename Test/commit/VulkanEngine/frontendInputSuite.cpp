// CPU-only tests for the shared frontend input/timing helpers.
//
// These functions route ALL keyboard/mouse input into the camera and scale it
// by the frame delta, and they had zero coverage - which is how the first
// frame's delta_time could be the entire startup wall clock (last_time starts
// at 0) and nothing noticed until a key held during load teleported the
// camera. Everything here is device-free by design; the two code paths that
// genuinely need a live GLFWwindow (ESC-to-close, cursor-callback swapping)
// are deliberately NOT exercised - a null window would hit real GLFW calls.

#include <gtest/gtest.h>

#include <GLFW/glfw3.h>
#include <imgui.h>

#include "shared/frontend/WindowInputState.hpp"

import kataglyphis.shared.frontend.frame_input;
import kataglyphis.shared.frontend.window_input_callbacks;

namespace {
using Kataglyphis::Frontend::clamp_frame_delta;
using Kataglyphis::Frontend::consume_axis_delta;
using Kataglyphis::Frontend::handle_key_callback;
using Kataglyphis::Frontend::handle_mouse_callback;
using Kataglyphis::Frontend::reset_window_keys;
using Kataglyphis::Frontend::should_capture_cursor;
using Kataglyphis::Frontend::should_release_cursor;
using Kataglyphis::Frontend::window_key_count;
}// namespace

TEST(FrameInputUnit, FirstFrameStartupSpikeIsClamped)
{
    // last_time starts at 0, so the first raw delta is the whole startup wall
    // clock. 7 seconds of construction must NOT become 7 seconds of camera
    // motion in one frame.
    EXPECT_FLOAT_EQ(clamp_frame_delta(7.3F), 0.1F);
}

TEST(FrameInputUnit, OrdinaryFrameDeltasPassThroughUnchanged)
{
    EXPECT_FLOAT_EQ(clamp_frame_delta(1.0F / 60.0F), 1.0F / 60.0F);
    EXPECT_FLOAT_EQ(clamp_frame_delta(0.1F), 0.1F);// boundary is inclusive
    EXPECT_FLOAT_EQ(clamp_frame_delta(0.0F), 0.0F);
}

TEST(FrameInputUnit, NegativeDeltasBecomeZero)
{
    // A clock adjustment must not run time backwards for dt consumers.
    EXPECT_FLOAT_EQ(clamp_frame_delta(-0.25F), 0.0F);
}

TEST(WindowInputUnit, KeyPressAndReleaseTrackTheArray)
{
    bool keys[window_key_count];
    reset_window_keys(keys);

    handle_key_callback(nullptr, keys, GLFW_KEY_W, GLFW_PRESS);
    EXPECT_TRUE(keys[GLFW_KEY_W]);
    handle_key_callback(nullptr, keys, GLFW_KEY_W, GLFW_RELEASE);
    EXPECT_FALSE(keys[GLFW_KEY_W]);
    // GLFW_REPEAT must not clear a held key.
    handle_key_callback(nullptr, keys, GLFW_KEY_A, GLFW_PRESS);
    handle_key_callback(nullptr, keys, GLFW_KEY_A, GLFW_REPEAT);
    EXPECT_TRUE(keys[GLFW_KEY_A]);
}

TEST(WindowInputUnit, OutOfRangeKeysAreIgnoredNotWritten)
{
    bool keys[window_key_count];
    reset_window_keys(keys);
    // GLFW_KEY_UNKNOWN is -1; media keys can exceed the array. Neither may
    // write out of bounds (this suite runs under ASan in CI, which enforces
    // exactly that).
    handle_key_callback(nullptr, keys, GLFW_KEY_UNKNOWN, GLFW_PRESS);
    handle_key_callback(nullptr, keys, window_key_count + 5, GLFW_PRESS);
    for (int i = 0; i < window_key_count; ++i) {
        ASSERT_FALSE(keys[i]) << "key " << i << " set by an out-of-range event";
    }
}

TEST(WindowInputUnit, FirstMouseMoveDoesNotJumpTheCamera)
{
    float last_x = 0.0F;
    float last_y = 0.0F;
    float x_change = 0.0F;
    float y_change = 0.0F;
    bool first_moved = true;

    // The first event after (re)capture must produce ZERO delta - otherwise
    // the camera snaps by the full distance between the stale last position
    // and wherever the cursor happens to be.
    handle_mouse_callback(nullptr, last_x, last_y, x_change, y_change, first_moved, 640.0, 360.0);
    EXPECT_FLOAT_EQ(x_change, 0.0F);
    EXPECT_FLOAT_EQ(y_change, 0.0F);
    EXPECT_FALSE(first_moved);

    // Subsequent moves: screen-space right is +x; y is INVERTED (screen y
    // grows downward, camera pitch grows upward).
    handle_mouse_callback(nullptr, last_x, last_y, x_change, y_change, first_moved, 650.0, 350.0);
    EXPECT_FLOAT_EQ(x_change, 10.0F);
    EXPECT_FLOAT_EQ(y_change, 10.0F);
}

TEST(WindowInputUnit, LookModeEntryReSeedsTheMouseOrigin)
{
    // A default-constructed WindowInputState must re-seed on its first
    // event, matching a freshly (re)entered look mode - otherwise the first
    // right-drag of every run snaps the camera by the cursor's absolute
    // screen position.
    Kataglyphis::Frontend::WindowInputState state;

    handle_mouse_callback(
      nullptr, state.last_x, state.last_y, state.x_change, state.y_change, state.mouse_first_moved, 640.0, 360.0);
    EXPECT_FLOAT_EQ(state.x_change, 0.0F);
    EXPECT_FLOAT_EQ(state.y_change, 0.0F);
}

TEST(WindowInputUnit, CursorCrossingAnImGuiPanelDoesNotJumpTheCamera)
{
    ImGuiContext *ctx = ImGui::CreateContext();
    ASSERT_NE(ctx, nullptr);

    float last_x = 0.0F;
    float last_y = 0.0F;
    float x_change = 0.0F;
    float y_change = 0.0F;
    bool first_moved = true;

    ImGui::GetIO().WantCaptureMouse = false;
    handle_mouse_callback(nullptr, last_x, last_y, x_change, y_change, first_moved, 100.0, 100.0);

    ImGui::GetIO().WantCaptureMouse = true;
    handle_mouse_callback(nullptr, last_x, last_y, x_change, y_change, first_moved, 400.0, 100.0);
    EXPECT_FLOAT_EQ(x_change, 0.0F) << "mouse input leaked past the ImGui capture gate";

    // Leaving the panel: the first event after capture ends must re-seed
    // against the cursor's current position, not difference against the
    // stale pre-panel position (which would produce 310, not 10).
    ImGui::GetIO().WantCaptureMouse = false;
    handle_mouse_callback(nullptr, last_x, last_y, x_change, y_change, first_moved, 410.0, 100.0);
    EXPECT_FLOAT_EQ(x_change, 10.0F);

    ImGui::DestroyContext(ctx);
}

TEST(WindowInputUnit, ConsumeAxisDeltaReturnsAndResets)
{
    float axis = 12.5F;
    EXPECT_FLOAT_EQ(consume_axis_delta(axis), 12.5F);
    EXPECT_FLOAT_EQ(axis, 0.0F);
    // Second consume without new motion must be zero - the bug this guards is
    // a camera that keeps rotating forever after one mouse move.
    EXPECT_FLOAT_EQ(consume_axis_delta(axis), 0.0F);
}

TEST(WindowInputUnit, ImGuiCaptureGateSwallowsInput)
{
    // The gate exists so typing in an ImGui text field does not also steer the
    // camera. It consults the CURRENT ImGui context, so build a real one.
    ImGuiContext *ctx = ImGui::CreateContext();
    ASSERT_NE(ctx, nullptr);
    ImGui::GetIO().WantCaptureKeyboard = true;
    ImGui::GetIO().WantCaptureMouse = true;

    bool keys[window_key_count];
    reset_window_keys(keys);
    handle_key_callback(nullptr, keys, GLFW_KEY_W, GLFW_PRESS);
    EXPECT_FALSE(keys[GLFW_KEY_W]) << "keyboard input leaked past the ImGui capture gate";

    float last_x = 0.0F;
    float last_y = 0.0F;
    float x_change = 0.0F;
    float y_change = 0.0F;
    bool first_moved = false;
    last_x = 100.0F;
    handle_mouse_callback(nullptr, last_x, last_y, x_change, y_change, first_moved, 200.0, 0.0);
    EXPECT_FLOAT_EQ(x_change, 0.0F) << "mouse input leaked past the ImGui capture gate";

    // With capture off, the same events must flow again.
    ImGui::GetIO().WantCaptureKeyboard = false;
    ImGui::GetIO().WantCaptureMouse = false;
    handle_key_callback(nullptr, keys, GLFW_KEY_W, GLFW_PRESS);
    EXPECT_TRUE(keys[GLFW_KEY_W]);

    ImGui::DestroyContext(ctx);
}

TEST(WindowInputUnit, ReleaseIsHonouredWhileImGuiHasCapture)
{
    // A key held when an ImGui widget takes focus must still be releasable -
    // otherwise the camera keeps moving forever once focus returns.
    ImGuiContext *ctx = ImGui::CreateContext();
    ASSERT_NE(ctx, nullptr);
    ImGui::GetIO().WantCaptureKeyboard = false;

    bool keys[window_key_count];
    reset_window_keys(keys);
    handle_key_callback(nullptr, keys, GLFW_KEY_W, GLFW_PRESS);
    EXPECT_TRUE(keys[GLFW_KEY_W]);

    ImGui::GetIO().WantCaptureKeyboard = true;
    handle_key_callback(nullptr, keys, GLFW_KEY_W, GLFW_RELEASE);
    EXPECT_FALSE(keys[GLFW_KEY_W]) << "release must fall through the ImGui capture gate";

    ImGui::DestroyContext(ctx);
}

TEST(WindowInputUnit, PressIsStillSwallowedWhileImGuiHasCapture)
{
    ImGuiContext *ctx = ImGui::CreateContext();
    ASSERT_NE(ctx, nullptr);
    ImGui::GetIO().WantCaptureKeyboard = true;

    bool keys[window_key_count];
    reset_window_keys(keys);
    handle_key_callback(nullptr, keys, GLFW_KEY_A, GLFW_PRESS);
    EXPECT_FALSE(keys[GLFW_KEY_A]) << "press must still be gated while ImGui has capture";

    ImGui::DestroyContext(ctx);
}

TEST(WindowInputUnit, CursorCaptureDecisionIgnoresImGuiState)
{
    // Covers should_capture_cursor only - the decision to (re)install the
    // cursor callback is a pure function of button/action, independent of
    // ImGui. should_release_cursor is covered separately below.
    EXPECT_TRUE(should_capture_cursor(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS));
    EXPECT_FALSE(should_capture_cursor(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE));
    EXPECT_FALSE(should_capture_cursor(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS));
}

TEST(WindowInputUnit, OnlyTheRightButtonReleaseEndsLookMode)
{
    EXPECT_TRUE(should_release_cursor(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE));
    EXPECT_FALSE(should_release_cursor(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS));
    EXPECT_FALSE(should_release_cursor(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS));
    EXPECT_FALSE(should_release_cursor(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE));
    EXPECT_FALSE(should_release_cursor(GLFW_MOUSE_BUTTON_MIDDLE, GLFW_RELEASE));
}
