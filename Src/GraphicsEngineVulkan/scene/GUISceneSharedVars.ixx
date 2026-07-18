module;

export module kataglyphis.vulkan.gui_scene_shared_vars;

export struct GUISceneSharedVars
{
    float direcional_light_radiance = 10.f;
    float directional_light_color[3] = { 1.f, 1.f, 1.f };
    float directional_light_direction[3] = { 0.075f, -1.f, 0.118f };

    // Shadows
    int shadow_map_res_index = 3;
    bool shadow_resolution_changed = false;
    int num_shadow_cascades = 3; // must stay <= MAX_CASCADES (SceneUBO array size)
    int pcf_radius = 2;
    float cascaded_shadow_intensity = 0.65f;
    const char* available_shadow_map_resolutions[4] = { "512", "1024", "2048", "4096" };

    // Clouds
    int cloud_speed = 6;
    int cloud_num_march_steps = 8;
    int cloud_num_march_steps_to_light = 3;
    float cloud_movement_direction[3] = { 1.f, 1.f, 1.f };
    float cloud_scale = 0.63f;
    float cloud_density = 0.493f;
    float cloud_pillowness = 0.966f;
    float cloud_cirrus_effect = 0.034f;
    bool cloud_powder_effect = true;
    bool clouds_enabled = false;
    float cloud_mesh_scale[3] = { 1000.f, 5.f, 1000.f };
    float cloud_mesh_offset[3] = { -0.364f, 367.f, -18.351f };

    // Shadows
    bool shadows_enabled = true;

    // Skybox
    bool skybox_enabled = true;

    // Model selection
    int selected_model_index = -1;
    bool model_reload_requested = false;
    bool model_transform_changed = false;
    float model_position[3] = { 0.f, 0.f, 0.f };
    float model_rotation[3] = { 0.f, 0.f, 0.f };
};
