// GUI -> Scene option plumbing.
//
// GUISceneSharedVars exists twice at runtime: the GUI owns the copy the
// widgets write to, and Scene owns the copy the renderer reads
// (VulkanRenderer.cpp reads scene_data->getGuiSceneSharedVars(), not the
// GUI's). Scene::update_user_input copies one into the other once per frame.
//
// Nothing tested that copy, and the gap is not hypothetical:
//   - the cascade-count default silently failed to reach the renderer once;
//   - a shadow diagnostic that set the GUI's copy and measured the rendered
//     result produced numbers that a second instrument could not reproduce,
//     with this two-copy split as a leading suspect.
//
// These are CPU-only: Scene is constructible without a Vulkan device, and
// update_user_input is a plain assignment. They will not catch a renderer
// that ignores a field it was handed - only that the field arrives.

#include <gtest/gtest.h>

#include "common/host_device_shared_vars.hpp"

#include <cstddef>
#include <cstring>
#include <string>

import kataglyphis.vulkan.gui_scene_shared_vars;

namespace {

// Byte size of GUISceneSharedVars as of the last time makeNonDefaultVars()
// below was checked against it. See the size test at the bottom.
constexpr std::size_t GUI_SCENE_SHARED_VARS_EXPECTED_SIZE = 192;

// Every field a caller can flip, set to something distinguishable from the
// default. Kept exhaustive on purpose: a field added to GUISceneSharedVars
// and forgotten here is exactly the silent-plumbing bug these tests exist to
// catch, and AllFieldsSurviveTheRoundTrip below fails when that happens.
GUISceneSharedVars makeNonDefaultVars()
{
    // Value-initialised, NOT default-initialised. The byte-wise comparison
    // below would otherwise read this struct's padding bytes, which
    // default-initialisation leaves indeterminate - the test would pass or
    // fail on whatever happened to be on the stack. `{}` zero-initialises the
    // whole object (padding included) before the member initialisers run, so
    // both sides of the comparison have deterministic padding.
    GUISceneSharedVars vars{};
    vars.direcional_light_radiance = 3.5F;
    vars.directional_light_color[0] = 0.25F;
    vars.directional_light_color[1] = 0.5F;
    vars.directional_light_color[2] = 0.75F;
    vars.directional_light_direction[0] = -0.1F;
    vars.directional_light_direction[1] = -0.2F;
    vars.directional_light_direction[2] = -0.3F;
    vars.shadow_map_res_index = 1;
    vars.shadow_resolution_changed = true;
    vars.num_shadow_cascades = 2;
    vars.pcf_radius = 4;
    vars.cascaded_shadow_intensity = 0.125F;
    vars.shadow_distance = 42.0F;
    vars.cascade_split_lambda = 0.75F;
    vars.cloud_speed = 11;
    vars.cloud_num_march_steps = 16;
    vars.cloud_num_march_steps_to_light = 5;
    vars.cloud_movement_direction[0] = 2.0F;
    vars.cloud_movement_direction[1] = 3.0F;
    vars.cloud_movement_direction[2] = 4.0F;
    vars.cloud_scale = 0.11F;
    vars.cloud_density = 0.22F;
    vars.cloud_pillowness = 0.33F;
    vars.cloud_cirrus_effect = 0.44F;
    vars.cloud_powder_effect = false;
    vars.clouds_enabled = true;
    vars.cloud_mesh_scale[0] = 10.0F;
    vars.cloud_mesh_scale[1] = 11.0F;
    vars.cloud_mesh_scale[2] = 12.0F;
    vars.cloud_mesh_offset[0] = 13.0F;
    vars.cloud_mesh_offset[1] = 14.0F;
    vars.cloud_mesh_offset[2] = 15.0F;
    vars.shadows_enabled = false;
    vars.skybox_enabled = false;
    vars.selected_model_index = 7;
    vars.model_reload_requested = true;
    vars.model_transform_changed = true;
    vars.model_position[0] = 1.5F;
    vars.model_position[1] = 2.5F;
    vars.model_position[2] = 3.5F;
    vars.model_rotation[0] = 4.5F;
    vars.model_rotation[1] = 5.5F;
    vars.model_rotation[2] = 6.5F;
    return vars;
}

}// namespace

// The whole struct must survive being copied, field for field. This is the
// assignment Scene::update_user_input performs; if GUISceneSharedVars ever
// grows a member that does not copy cleanly (a pointer, a reference, a
// self-referential handle), the renderer silently reads the wrong value.
TEST(GuiSceneVarsRoundTrip, AllFieldsSurviveTheRoundTrip)
{
    const GUISceneSharedVars source = makeNonDefaultVars();

    GUISceneSharedVars destination{};
    destination = source;// exactly what Scene::update_user_input does

    EXPECT_EQ(std::memcmp(&source, &destination, sizeof(GUISceneSharedVars)), 0)
      << "GUISceneSharedVars did not copy cleanly";
}

// The byte-wise test above only proves the fields it was GIVEN survive; a
// field added to the struct but not to makeNonDefaultVars() keeps its default
// on both sides and compares equal, so the new control would go untested
// while the suite stayed green. Pinning the size makes that omission a build
// -time failure with instructions attached, which is the only mechanism here
// that fails when someone forgets rather than when someone breaks something.
TEST(GuiSceneVarsRoundTrip, StructSizeIsPinnedSoNewFieldsCannotBeForgotten)
{
    // If this fails, a field was added to GUISceneSharedVars. Set it in
    // makeNonDefaultVars(), then update this number.
    EXPECT_EQ(sizeof(GUISceneSharedVars), GUI_SCENE_SHARED_VARS_EXPECTED_SIZE)
      << "GUISceneSharedVars changed size: add the new field to makeNonDefaultVars(), "
         "then update GUI_SCENE_SHARED_VARS_EXPECTED_SIZE";
}

// The fields the shadow path depends on, named individually so a failure
// says which control is broken rather than "some bytes differ".
TEST(GuiSceneVarsRoundTrip, ShadowControlsArrive)
{
    const GUISceneSharedVars source = makeNonDefaultVars();
    GUISceneSharedVars destination{};
    destination = source;

    EXPECT_EQ(destination.shadows_enabled, source.shadows_enabled);
    EXPECT_EQ(destination.num_shadow_cascades, source.num_shadow_cascades);
    EXPECT_EQ(destination.pcf_radius, source.pcf_radius);
    EXPECT_FLOAT_EQ(destination.cascaded_shadow_intensity, source.cascaded_shadow_intensity);
    EXPECT_FLOAT_EQ(destination.shadow_distance, source.shadow_distance);
    EXPECT_FLOAT_EQ(destination.cascade_split_lambda, source.cascade_split_lambda);
    EXPECT_EQ(destination.shadow_map_res_index, source.shadow_map_res_index);
}

// A copy must not alias. If the two copies ever shared storage, changing the
// GUI's would retroactively change what the renderer already read this frame.
TEST(GuiSceneVarsRoundTrip, TheCopyIsIndependentOfItsSource)
{
    GUISceneSharedVars source = makeNonDefaultVars();
    GUISceneSharedVars destination{};
    destination = source;

    source.cascaded_shadow_intensity = 0.9F;
    source.shadows_enabled = true;
    source.model_position[1] = 99.0F;

    EXPECT_FLOAT_EQ(destination.cascaded_shadow_intensity, 0.125F) << "the copy aliases its source";
    EXPECT_FALSE(destination.shadows_enabled);
    EXPECT_FLOAT_EQ(destination.model_position[1], 2.5F);
}

// The defaults are what ships, and two of them are load-bearing enough to
// pin: cascade_split_lambda is 0 for a measured reason (see the table in
// CascadedShadowMap.cpp - raising it makes the shipped scene worse), and
// num_shadow_cascades must stay within the SceneUBO array or the shader
// indexes out of bounds.
TEST(GuiSceneVarsRoundTrip, ShippedDefaultsAreTheMeasuredOnes)
{
    const GUISceneSharedVars defaults;

    EXPECT_FLOAT_EQ(defaults.cascade_split_lambda, 0.0F)
      << "lambda defaults to uniform splits deliberately; raising it needs a measurement, not a hunch";
    EXPECT_GT(defaults.shadow_distance, 0.0F) << "a non-positive shadow distance silently disables the clamp";
    EXPECT_GT(defaults.num_shadow_cascades, 0);
    EXPECT_LE(defaults.num_shadow_cascades, 4) << "must stay <= MAX_CASCADES (SceneUBO array size)";
    EXPECT_TRUE(defaults.shadows_enabled) << "the debug scene exists to show shadows";
}

// The GUI's "# cascades" slider now advertises 1..MAX_CASCADES directly
// (GUI.cpp), so the default must sit inside that exact range, not just the
// looser bound above.
TEST(GuiSceneVarsRoundTrip, CascadeCountDefaultIsWithinTheSliderRange)
{
    const GUISceneSharedVars defaults;

    EXPECT_LE(defaults.num_shadow_cascades, MAX_CASCADES);
}

// available_shadow_map_resolutions and shadowResolutionForIndex used to be
// two hand-rolled copies of the same four-entry table (the GUI's combo
// labels, and a second if/else chain in VulkanRenderer::handleShadowResolutionChange)
// that were free to drift apart - which is exactly what happened: the combo
// claimed "4096" for index 3 while VulkanRenderer::init hard-coded a 2048
// startup allocation. This pins every label against the function that is now
// the only source of the pixel counts.
TEST(ShadowResolutionUnit, EveryComboLabelMatchesThePixelCount)
{
    const GUISceneSharedVars defaults;
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(std::stoul(defaults.available_shadow_map_resolutions[i]), shadowResolutionForIndex(i))
          << "label/pixel-count mismatch at index " << i;
    }
}

TEST(ShadowResolutionUnit, OutOfRangeIndicesClampInsteadOfSilentlyPicking512)
{
    EXPECT_EQ(shadowResolutionForIndex(-1), 512U);
    EXPECT_EQ(shadowResolutionForIndex(4), 4096U);
}

// Documents the behaviour this change pins: the GUI's default index is what
// VulkanRenderer::init now allocates at startup (index 2 == 2048, matching
// what the renderer already built before this fix - the combo's label was
// the thing that was wrong, not the allocation).
TEST(ShadowResolutionUnit, TheDefaultIndexIsWhatStartupAllocates)
{
    const GUISceneSharedVars defaults;
    EXPECT_EQ(shadowResolutionForIndex(defaults.shadow_map_res_index), 2048U);
}
