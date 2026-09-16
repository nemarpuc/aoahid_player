// SPDX-License-Identifier: MIT
#include "theme.hpp"

#include <cstdio>
#include <fstream>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// Generated at build time from Dear ImGui's bundled Roboto-Medium.ttf.
extern const unsigned char aoahid_player_font_data[];
extern const unsigned int aoahid_player_font_size;

namespace gui::theme {
namespace {

bool file_exists(const std::string& path) { return std::ifstream(path, std::ios::binary).good(); }

// A CJK-capable font already on the system, so Japanese (and other CJK)
// paths and filenames render instead of tofu boxes. Not embedded: even one
// trimmed Noto Sans CJK weight runs several MB, working against this
// project's low-resource-over-polish priority (see the priorities in
// README.md); Roboto alone, embedded, stays a tiny build-time asset.
// Returns an empty string if nothing suitable was found.
std::string find_cjk_font_path() {
#if defined(_WIN32)
    char windir[MAX_PATH];
    const UINT length = GetWindowsDirectoryA(windir, sizeof windir);
    if (length == 0 || length >= sizeof windir)
        return {};
    static const char* const candidates[] = {"\\Fonts\\Meiryo.ttc", "\\Fonts\\YuGothM.ttc",
                                             "\\Fonts\\msgothic.ttc"};
    for (const char* suffix : candidates) {
        std::string path = std::string(windir) + suffix;
        if (file_exists(path))
            return path;
    }
    return {};
#elif defined(__APPLE__)
    static const char* const candidates[] = {
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
    };
    for (const char* path : candidates) {
        if (file_exists(path))
            return path;
    }
    return {};
#else
    // Asks fontconfig for whatever font it would already pick to render
    // Japanese, the same lookup the rest of the desktop uses; a fixed
    // command with no external input, so this is not a shell-injection
    // concern.
    std::string result;
    FILE* pipe = popen("fc-match -f '%{file}' ':lang=ja' 2>/dev/null", "r");
    if (pipe != nullptr) {
        char buffer[512];
        if (std::fgets(buffer, sizeof buffer, pipe) != nullptr)
            result = buffer;
        pclose(pipe);
    }
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    if (!result.empty() && file_exists(result))
        return result;
    static const char* const candidates[] = {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    };
    for (const char* path : candidates) {
        if (file_exists(path))
            return path;
    }
    return {};
#endif
}

} // namespace

void apply_style(const float dpi_scale) {
    ImGuiStyle style;
    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(10, 7);
    style.CellPadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(10, 9);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.IndentSpacing = 22;
    style.ScrollbarSize = 9;
    style.GrabMinSize = 10;
    style.WindowBorderSize = 0;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 0;
    style.WindowRounding = 0;
    style.ChildRounding = 10;
    style.FrameRounding = 6;
    style.PopupRounding = 8;
    style.ScrollbarRounding = 6;
    style.GrabRounding = 4;
    style.TabRounding = 6;
    style.SeparatorTextBorderSize = 1;
    style.SelectableTextAlign = ImVec2(0, 0.5f);
    style.DisabledAlpha = 0.4f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = vec(text);
    c[ImGuiCol_TextDisabled] = vec(text_faint);
    c[ImGuiCol_WindowBg] = vec(background);
    c[ImGuiCol_ChildBg] = vec(surface);
    c[ImGuiCol_PopupBg] = vec(rgb(0x1A1B20));
    c[ImGuiCol_Border] = vec(border);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = vec(field);
    c[ImGuiCol_FrameBgHovered] = vec(field_hover);
    c[ImGuiCol_FrameBgActive] = vec(field_active);
    c[ImGuiCol_TitleBg] = vec(background);
    c[ImGuiCol_TitleBgActive] = vec(background);
    c[ImGuiCol_TitleBgCollapsed] = vec(background);
    c[ImGuiCol_MenuBarBg] = vec(surface);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = vec(border_strong);
    c[ImGuiCol_ScrollbarGrabHovered] = vec(rgb(0x44454E));
    c[ImGuiCol_ScrollbarGrabActive] = vec(rgb(0x55565F));
    c[ImGuiCol_CheckMark] = vec(rgb(0xFFFFFF));
    c[ImGuiCol_SliderGrab] = vec(accent);
    c[ImGuiCol_SliderGrabActive] = vec(accent_hover);
    c[ImGuiCol_Button] = vec(field);
    c[ImGuiCol_ButtonHovered] = vec(field_hover);
    c[ImGuiCol_ButtonActive] = vec(field_active);
    c[ImGuiCol_Header] = vec(accent_soft);
    c[ImGuiCol_HeaderHovered] = vec(field_hover);
    c[ImGuiCol_HeaderActive] = vec(field_active);
    c[ImGuiCol_Separator] = vec(border);
    c[ImGuiCol_SeparatorHovered] = vec(accent);
    c[ImGuiCol_SeparatorActive] = vec(accent);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripActive] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Tab] = vec(surface);
    c[ImGuiCol_TabHovered] = vec(field_hover);
    c[ImGuiCol_TabSelected] = vec(field);
    c[ImGuiCol_TextSelectedBg] = vec(rgb(0x6E5FD1, 110));
    c[ImGuiCol_NavCursor] = vec(accent);
    c[ImGuiCol_ModalWindowDimBg] = vec(rgb(0x000000, 140));

    style.ScaleAllSizes(dpi_scale);
    style.FontSizeBase = font_body;
    style.FontScaleDpi = dpi_scale;
    ImGui::GetStyle() = style;
}

void setup(const float dpi_scale) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false; // static data, never freed
    config.OversampleH = 2;
    std::snprintf(config.Name, sizeof config.Name, "Roboto Medium");
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(aoahid_player_font_data),
                                   static_cast<int>(aoahid_player_font_size), font_body, &config);

    // Merged into the same logical font as Roboto (which has no CJK
    // glyphs), so Japanese paths and filenames render instead of tofu
    // boxes wherever a script or profile name happens to use them. No
    // glyph range is specified: since 1.92, with a renderer backend that
    // supports ImGuiBackendFlags_RendererHasTextures (true here, see
    // imgui_impl_opengl3.cpp), glyphs rasterize on demand from whatever
    // codepoints actually appear, so pre-declaring Japanese's ranges is
    // unnecessary (and GetGlyphRangesJapanese() is gone under this
    // project's IMGUI_DISABLE_OBSOLETE_FUNCTIONS).
    const std::string cjk_path = find_cjk_font_path();
    if (!cjk_path.empty()) {
        ImFontConfig cjk_config;
        cjk_config.MergeMode = true;
        cjk_config.OversampleH = 2;
        std::snprintf(cjk_config.Name, sizeof cjk_config.Name, "CJK fallback");
        io.Fonts->AddFontFromFileTTF(cjk_path.c_str(), font_body, &cjk_config);
    }
    apply_style(dpi_scale);
}

} // namespace gui::theme
