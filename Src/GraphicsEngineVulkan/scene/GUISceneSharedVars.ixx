module;

export module kataglyphis.vulkan.gui_scene_shared_vars;

export struct GUISceneSharedVars
{
    float direcional_light_radiance = 10.f;
    float directional_light_color[3] = { 1.f, 1.f, 1.f };
    float directional_light_direction[3] = { 0.075f, -1.f, 0.118f };
};
