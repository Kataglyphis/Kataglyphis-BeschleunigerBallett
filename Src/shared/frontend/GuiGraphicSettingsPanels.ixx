module;

#include <imgui.h>

export module kataglyphis.shared.frontend.gui_graphic_settings_panels;

import kataglyphis.shared.frontend.gui_scene_shared_vars;

export namespace Kataglyphis::Frontend {

inline void renderDirectionalLightSettings(GUISceneSharedVars& guiSceneSharedVars) {
    if (ImGui::TreeNode("Directional Light")) {
        ImGui::Separator();
        ImGui::SliderFloat("Radiance / Ambient intensity", &guiSceneSharedVars.direcional_light_radiance, 0.0F, 50.0F);
        ImGui::Separator();
        // Edit a color (stored as ~4 floats)
        ImGui::ColorEdit3("Directional Light Color", guiSceneSharedVars.directional_light_color);
        ImGui::Separator();
        ImGui::SliderFloat3("Light Direction", guiSceneSharedVars.directional_light_direction, -1.F, 1.0F);

        ImGui::TreePop();
    }
}

inline void renderHotShaderReload(bool& shader_hot_reload_triggered) {
    if (ImGui::CollapsingHeader("Hot shader reload")) {
        if (ImGui::Button("All shader!")) { shader_hot_reload_triggered = true; }
    }
}

}
