module;

#include <iomanip>
#include <sstream>

#include <imgui.h>

export module kataglyphis.shared.frontend.common_gui_panels;

export namespace Kataglyphis::Frontend {

inline void renderCommonGuiStyleSettings()
{
    constexpr float max_rounding = 12.0F;

    if (ImGui::CollapsingHeader("GUI Settings")) {
        ImGuiStyle &style = ImGui::GetStyle();

        if (ImGui::SliderFloat("Frame Rounding", &style.FrameRounding, 0.0F, max_rounding, "%.0f")) {
            style.GrabRounding = style.FrameRounding;
        }

        bool border = (style.FrameBorderSize > 0.0F);
        if (ImGui::Checkbox("FrameBorder", &border)) { style.FrameBorderSize = border ? 1.0F : 0.0F; }

        ImGui::SliderFloat("WindowRounding", &style.WindowRounding, 0.0F, max_rounding, "%.0f");
    }
}

inline void renderCommonKeyBindings()
{
    if (ImGui::CollapsingHeader("KEY Bindings")) {
        ImGui::TextUnformatted("WASD for moving Forward, backward and to the side\n QE for rotating ");
    }
}

inline void renderCommonFrameStats()
{
    constexpr double millis_per_second = 1000.0;

    auto const fps = static_cast<double>(ImGui::GetIO().Framerate);
    double const frame_time_ms = fps > 0.0 ? (millis_per_second / fps) : 0.0;

    std::ostringstream text;
    text << std::fixed;
    if (fps > 0.0) {
        text << "Application average " << std::setprecision(3) << frame_time_ms << " ms/frame (" << std::setprecision(1)
             << fps << " FPS)";
    } else {
        text << "Application average N/A (" << std::setprecision(1) << fps << " FPS)";
    }
    ImGui::TextUnformatted(text.str().c_str());
}

// Per-pass GPU timings panel. `pass_ms` holds `pass_count` smoothed values in
// milliseconds; entries < 0 render as "n/a" (pass disabled or no sample yet).
// When `supported` is false (no timestamp support on the queue family) the
// panel only shows "unavailable".
inline void renderGpuTimingsPanel(bool supported, const float *pass_ms, const char *const *pass_names, int pass_count)
{
    if (!ImGui::CollapsingHeader("GPU timings")) { return; }

    if (!supported) {
        ImGui::TextDisabled("unavailable (no timestamp support on this queue family)");
        return;
    }

    for (int i = 0; i < pass_count; i++) {
        if (pass_ms[i] >= 0.0F) {
            ImGui::Text("%-18s %6.3f ms", pass_names[i], static_cast<double>(pass_ms[i]));
        } else {
            ImGui::Text("%-18s    n/a", pass_names[i]);
        }
    }
}

}// namespace Kataglyphis::Frontend
