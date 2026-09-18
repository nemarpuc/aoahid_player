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

// Two full palettes, assigned wholesale so nothing is left half-updated.
// `accent` and `accent2` swap identities between them (purple leads in dark,
// teal leads in light) rather than either colour just getting darker or
// lighter, since a flipped light theme needs new values for nearly
// everything else anyway.
void set_mode(const bool dark) {
    if (dark) {
        background = rgb(0x0B0C0E);
        surface = rgb(0x14151A);
        surface_hi = rgb(0x1A1B21);
        field = rgb(0x1E1F25);
        field_hover = rgb(0x272830);
        field_active = rgb(0x2F3039);
        border = rgb(0x22242B);
        border_strong = rgb(0x33353D);
        text = rgb(0xE8E9ED);
        text_dim = rgb(0x9D9EA9);
        text_faint = rgb(0x83848F);

        // Black text on `accent` is about 8.6:1 (a light purple reads better
        // with dark text than white).
        accent = rgb(0x9C8CE6);
        accent_hover = rgb(0xAC9EEC);
        accent_active = rgb(0x8A78D8);
        accent_text = rgb(0xC7BBF5);
        accent_soft = rgb(0x9C8CE6, 40);
        accent_line = rgb(0x9C8CE6, 140);
        accent_ink = rgb(0x1B1030);

        accent2 = rgb(0x4FD1C5);
        accent2_soft = rgb(0x4FD1C5, 40);
        accent2_line = rgb(0x4FD1C5, 140);
        accent2_text = rgb(0x8FE4DA);

        success = rgb(0x4DB885);
        warning = rgb(0xD8A445);
        danger = rgb(0xE0564E);
        danger_soft = rgb(0xE0564E, 36);

        popup_bg = rgb(0x1A1B20);
        scrollbar_hover = rgb(0x44454E);
        scrollbar_active = rgb(0x55565F);
        check_mark = rgb(0xFFFFFF);
        text_selected_bg = rgb(0x9C8CE6, 110);
    } else {
        background = rgb(0xF5F5F8);
        surface = rgb(0xFFFFFF);
        surface_hi = rgb(0xF0F0F4);
        field = rgb(0xEAEBF0);
        field_hover = rgb(0xDFE0E7);
        field_active = rgb(0xD2D3DC);
        border = rgb(0xDBDCE2);
        border_strong = rgb(0xC3C4CE);
        text = rgb(0x191A1F);
        text_dim = rgb(0x53555F);
        text_faint = rgb(0x6C6E78); // about 4.9:1 on `background`

        // The same teal as the dark theme's second accent, kept exactly —
        // only the derived text/line shades below are tuned for a white
        // page instead of a black one.
        accent = rgb(0x4FD1C5);
        accent_hover = rgb(0x63DBD0);
        accent_active = rgb(0x3FB9AE);
        accent_text = rgb(0x0E8F80); // deep enough for text on white
        accent_soft = rgb(0x4FD1C5, 45);
        accent_line = rgb(0x4FD1C5, 160);
        accent_ink = rgb(0x06201C); // still reads on the same light fill

        accent2 = rgb(0x7C6AD8);
        accent2_soft = rgb(0x7C6AD8, 45);
        accent2_line = rgb(0x7C6AD8, 160);
        accent2_text = rgb(0x5D4BC0);

        success = rgb(0x1E8F5F);
        warning = rgb(0xAD7A12);
        danger = rgb(0xC63B33);
        danger_soft = rgb(0xC63B33, 40);

        popup_bg = rgb(0xFCFCFE);
        scrollbar_hover = rgb(0xB9BAC4);
        scrollbar_active = rgb(0xA5A6B2);
        check_mark = rgb(0x1B1030);
        text_selected_bg = rgb(0x4FD1C5, 90);
    }
}

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
    style.ChildRounding = 4;
    style.FrameRounding = 3;
    style.PopupRounding = 4;
    style.ScrollbarRounding = 3;
    style.GrabRounding = 2;
    style.TabRounding = 3;
    style.SeparatorTextBorderSize = 1;
    style.SelectableTextAlign = ImVec2(0, 0.5f);
    style.DisabledAlpha = 0.4f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = vec(text);
    c[ImGuiCol_TextDisabled] = vec(text_faint);
    c[ImGuiCol_WindowBg] = vec(background);
    c[ImGuiCol_ChildBg] = vec(surface);
    c[ImGuiCol_PopupBg] = vec(popup_bg);
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
    c[ImGuiCol_ScrollbarGrabHovered] = vec(scrollbar_hover);
    c[ImGuiCol_ScrollbarGrabActive] = vec(scrollbar_active);
    c[ImGuiCol_CheckMark] = vec(check_mark);
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
    c[ImGuiCol_TextSelectedBg] = vec(text_selected_bg);
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
