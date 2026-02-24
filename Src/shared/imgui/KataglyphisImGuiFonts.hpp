#pragma once

#include <array>
#include <filesystem>

#include <imgui.h>

namespace Kataglyphis::Frontend {
inline auto resolveKataglyphisImGuiFontDirectory(const std::filesystem::path &cwd) -> std::filesystem::path
{
#ifdef RELATIVE_IMGUI_FONTS_PATH
    auto fromMacro = (cwd / std::filesystem::path(RELATIVE_IMGUI_FONTS_PATH)).lexically_normal();
    if (std::filesystem::exists(fromMacro)) { return fromMacro; }
#endif

    constexpr int maxSearchDepth = 8;
    std::filesystem::path current = cwd;
    for (int depth = 0; depth < maxSearchDepth; ++depth) {
        const auto candidate = (current / "ExternalLib/IMGUI/misc/fonts").lexically_normal();
        if (std::filesystem::exists(candidate)) { return candidate; }

        if (!current.has_parent_path()) { break; }
        const auto parent = current.parent_path();
        if (parent == current) { break; }
        current = parent;
    }

    return {};
}

inline auto addKataglyphisFontIfAvailable(ImFontAtlas *fonts, const std::filesystem::path &fontPath, float sizePixels)
  -> bool
{
    if (!std::filesystem::exists(fontPath)) { return false; }

    return fonts->AddFontFromFileTTF(fontPath.string().c_str(), sizePixels) != nullptr;
}

inline void configureKataglyphisImGuiFonts(ImGuiIO &imguiIo, float sizePixels)
{
    const std::filesystem::path fontDir = resolveKataglyphisImGuiFontDirectory(std::filesystem::current_path());

    bool hasCustomFont = false;
    if (!fontDir.empty()) {
        const std::array<std::filesystem::path, 6> fontFiles = { fontDir / "Roboto-Medium.ttf",
            fontDir / "Cousine-Regular.ttf",
            fontDir / "DroidSans.ttf",
            fontDir / "Karla-Regular.ttf",
            fontDir / "ProggyClean.ttf",
            fontDir / "ProggyTiny.ttf" };

        for (const auto &fontPath : fontFiles) {
            hasCustomFont = addKataglyphisFontIfAvailable(imguiIo.Fonts, fontPath, sizePixels) || hasCustomFont;
        }
    }

    if (!hasCustomFont) {
        ImFontConfig fontConfig{};
        fontConfig.SizePixels = sizePixels;
        imguiIo.Fonts->AddFontDefault(&fontConfig);
    }
}

inline void configureKataglyphisImGuiFonts(ImGuiIO &imguiIo)
{
    constexpr float defaultFontSizePixels = 30.0F;
    configureKataglyphisImGuiFonts(imguiIo, defaultFontSizePixels);
}
}// namespace Kataglyphis::Frontend
