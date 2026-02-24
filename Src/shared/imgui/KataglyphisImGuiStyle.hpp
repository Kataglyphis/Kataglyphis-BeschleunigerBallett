#pragma once

#include <imgui.h>

namespace Kataglyphis::Frontend {
inline void applyKataglyphisImGuiDarkTheme()
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    constexpr ImVec4 primary = ImVec4(0.4118F, 0.9412F, 0.6824F, 1.00F);
    constexpr ImVec4 primaryHover = ImVec4(0.4900F, 0.9600F, 0.7400F, 1.00F);
    constexpr ImVec4 primaryActive = ImVec4(0.3200F, 0.8600F, 0.6100F, 1.00F);

    colors[ImGuiCol_Text] = ImVec4(0.95F, 0.97F, 0.96F, 1.00F);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.46F, 0.50F, 0.48F, 1.00F);
    colors[ImGuiCol_WindowBg] = ImVec4(0.00F, 0.00F, 0.00F, 0.98F);
    colors[ImGuiCol_ChildBg] = ImVec4(0.01F, 0.01F, 0.01F, 0.98F);
    colors[ImGuiCol_PopupBg] = ImVec4(0.03F, 0.03F, 0.03F, 0.99F);
    colors[ImGuiCol_Border] = ImVec4(primary.x, primary.y, primary.z, 0.85F);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00F, 0.00F, 0.00F, 0.00F);

    colors[ImGuiCol_FrameBg] = ImVec4(primary.x, primary.y, primary.z, 0.26F);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(primary.x, primary.y, primary.z, 0.32F);
    colors[ImGuiCol_FrameBgActive] = ImVec4(primary.x, primary.y, primary.z, 0.44F);

    colors[ImGuiCol_TitleBg] = ImVec4(0.00F, 0.00F, 0.00F, 1.00F);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.06F, 0.10F, 0.08F, 1.00F);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00F, 0.00F, 0.00F, 0.96F);

    colors[ImGuiCol_MenuBarBg] = ImVec4(0.02F, 0.02F, 0.02F, 1.00F);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02F, 0.02F, 0.02F, 1.00F);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.18F, 0.20F, 0.19F, 1.00F);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(primary.x, primary.y, primary.z, 0.70F);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(primary.x, primary.y, primary.z, 0.95F);

    colors[ImGuiCol_CheckMark] = primary;
    colors[ImGuiCol_SliderGrab] = primary;
    colors[ImGuiCol_SliderGrabActive] = primaryActive;

    colors[ImGuiCol_Button] = ImVec4(primary.x, primary.y, primary.z, 0.26F);
    colors[ImGuiCol_ButtonHovered] = ImVec4(primaryHover.x, primaryHover.y, primaryHover.z, 0.52F);
    colors[ImGuiCol_ButtonActive] = ImVec4(primaryActive.x, primaryActive.y, primaryActive.z, 0.72F);

    colors[ImGuiCol_Header] = ImVec4(primary.x, primary.y, primary.z, 0.24F);
    colors[ImGuiCol_HeaderHovered] = ImVec4(primaryHover.x, primaryHover.y, primaryHover.z, 0.44F);
    colors[ImGuiCol_HeaderActive] = ImVec4(primaryActive.x, primaryActive.y, primaryActive.z, 0.60F);

    colors[ImGuiCol_Separator] = ImVec4(0.17F, 0.20F, 0.18F, 1.00F);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(primary.x, primary.y, primary.z, 0.66F);
    colors[ImGuiCol_SeparatorActive] = ImVec4(primary.x, primary.y, primary.z, 0.90F);

    colors[ImGuiCol_ResizeGrip] = ImVec4(primary.x, primary.y, primary.z, 0.30F);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(primaryHover.x, primaryHover.y, primaryHover.z, 0.55F);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(primaryActive.x, primaryActive.y, primaryActive.z, 0.75F);

    colors[ImGuiCol_Tab] = ImVec4(0.04F, 0.04F, 0.04F, 1.00F);
    colors[ImGuiCol_TabHovered] = ImVec4(primaryHover.x, primaryHover.y, primaryHover.z, 0.58F);
    colors[ImGuiCol_TabSelected] = ImVec4(primary.x, primary.y, primary.z, 0.46F);
    colors[ImGuiCol_TabSelectedOverline] = primary;
    colors[ImGuiCol_TabDimmed] = ImVec4(0.02F, 0.02F, 0.02F, 1.00F);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(primary.x, primary.y, primary.z, 0.30F);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(primary.x, primary.y, primary.z, 0.72F);

    colors[ImGuiCol_PlotLines] = ImVec4(0.65F, 0.75F, 0.70F, 1.00F);
    colors[ImGuiCol_PlotLinesHovered] = primary;
    colors[ImGuiCol_PlotHistogram] = primary;
    colors[ImGuiCol_PlotHistogramHovered] = primaryHover;

    colors[ImGuiCol_TextSelectedBg] = ImVec4(primary.x, primary.y, primary.z, 0.40F);
    colors[ImGuiCol_DragDropTarget] = primary;
    colors[ImGuiCol_NavCursor] = primary;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(primary.x, primary.y, primary.z, 0.75F);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.04F, 0.08F, 0.06F, 1.00F);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.15F, 0.24F, 0.20F, 1.00F);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.10F, 0.16F, 0.13F, 1.00F);

    style.WindowRounding = 10.0F;
    style.FrameRounding = 8.0F;
    style.GrabRounding = 8.0F;
    style.ScrollbarRounding = 10.0F;
    style.TabRounding = 8.0F;
    style.WindowBorderSize = 1.0F;
    style.FrameBorderSize = 2.0F;
    style.PopupBorderSize = 1.0F;
    style.IndentSpacing = 14.0F;
    style.WindowPadding = ImVec2(12.0F, 10.0F);
    style.FramePadding = ImVec2(10.0F, 5.0F);
    style.ItemSpacing = ImVec2(9.0F, 8.0F);
    style.ItemInnerSpacing = ImVec2(7.0F, 6.0F);
    style.ScrollbarSize = 14.0F;
    style.GrabMinSize = 12.0F;
}
}// namespace Kataglyphis::Frontend
