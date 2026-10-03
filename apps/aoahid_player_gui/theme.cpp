// SPDX-License-Identifier: MIT
#include "theme.hpp"

#include <algorithm>
#include <cmath>
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

namespace {
unsigned glass_tint = 0x161B22; // `surface` without its alpha
float glass_transparency = 0.5f;
bool dark_mode = true;
bool glass_on = false;
unsigned accent_seed = 0x388BFD;
// Each mode's own gloss and rim strengths, before set_shine() scales them.
unsigned gloss_base = 20;
unsigned rim_top_base = 80;
unsigned rim_bottom_base = 14;
float gloss_scale = 1.0f;
float rim_scale = 1.0f;

struct Rgb {
    float r, g, b; // 0..1, sRGB
};

Rgb unpack(const unsigned hex) {
    return {static_cast<float>((hex >> 16) & 0xFF) / 255.0f,
            static_cast<float>((hex >> 8) & 0xFF) / 255.0f, static_cast<float>(hex & 0xFF) / 255.0f};
}

unsigned pack(const Rgb c) {
    const auto channel = [](const float value) {
        return static_cast<unsigned>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    return channel(c.r) << 16 | channel(c.g) << 8 | channel(c.b);
}

unsigned mix(const unsigned a, const unsigned b, const float t) {
    const Rgb x = unpack(a);
    const Rgb y = unpack(b);
    return pack({x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t});
}

// WCAG relative luminance and contrast ratio.
float luminance(const unsigned hex) {
    const Rgb c = unpack(hex);
    const auto linear = [](const float v) {
        return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
    };
    return 0.2126f * linear(c.r) + 0.7152f * linear(c.g) + 0.0722f * linear(c.b);
}

float contrast(const unsigned a, const unsigned b) {
    const float x = luminance(a);
    const float y = luminance(b);
    return (std::max(x, y) + 0.05f) / (std::min(x, y) + 0.05f);
}

// The same hue, more saturated, and dark enough for white text (4.5:1).
unsigned deepen(const unsigned hex) {
    const Rgb c = unpack(hex);
    const float high = std::max({c.r, c.g, c.b});
    const float low = std::min({c.r, c.g, c.b});
    // Saturation up: push each channel away from the maximum.
    const float saturation = high > 0.0f ? (high - low) / high : 0.0f;
    const float target = std::min(1.0f, saturation * 1.6f + 0.15f);
    const float stretch = saturation > 0.0f ? target / saturation : 1.0f;
    Rgb vivid{high - (high - c.r) * stretch, high - (high - c.g) * stretch,
              high - (high - c.b) * stretch};
    if (saturation == 0.0f)
        vivid = c;
    unsigned result = pack(vivid);
    for (float value = 1.0f; value > 0.2f && contrast(result, 0xFFFFFF) < 4.5f; value -= 0.02f)
        result = pack({vivid.r * value / high, vivid.g * value / high, vivid.b * value / high});
    return result;
}

// Lighter until it stands out from `ground` by `ratio`.
unsigned lift_on(unsigned hex, const unsigned ground, const float ratio) {
    for (int step = 0; step < 40 && contrast(hex, ground) < ratio; ++step)
        hex = mix(hex, 0xFFFFFF, 0.05f);
    return hex;
}

// Darker until it reads as text on `ground` (4.5:1).
unsigned readable_on(unsigned hex, const unsigned ground) {
    for (int step = 0; step < 40 && contrast(hex, ground) < 4.5f; ++step)
        hex = mix(hex, 0x000000, 0.05f);
    return hex;
}
} // namespace

void set_accent(const unsigned seed) {
    accent_seed = seed & 0xFFFFFFU;
    if (dark_mode) {
        // A very dark choice is lifted until it shows against the cards (3:1,
        // as for graphics); its text shade until it reads (4.5:1).
        const unsigned ground = glass_on ? 0x2A2833 : 0x161B22;
        const unsigned base = lift_on(accent_seed, ground, 3.0f);
        accent = rgb(base);
        accent_hover = rgb(mix(base, 0xFFFFFF, 0.2f));
        accent_active = rgb(mix(base, 0x000000, 0.12f));
        accent_text = rgb(lift_on(mix(base, 0xFFFFFF, 0.35f), ground, 4.5f));
        accent_soft = rgb(base, 44);
        accent_line = rgb(base, 150);
        accent_ink = rgb(contrast(base, 0x0D1117) >= 4.5f ? 0x0D1117 : 0xFFFFFF);
        text_selected_bg = rgb(base, 110);
    } else {
        const unsigned base = deepen(accent_seed);
        const unsigned ground = glass_on ? 0xECEAF3 : 0xFFFFFF;
        accent = rgb(base);
        accent_hover = rgb(mix(base, 0xFFFFFF, 0.12f));
        accent_active = rgb(mix(base, 0x000000, 0.12f));
        accent_text = rgb(readable_on(base, ground));
        accent_soft = rgb(base, 40);
        accent_line = rgb(base, 160);
        accent_ink = rgb(0xFFFFFF);
        text_selected_bg = rgb(base, 70);
    }
}

void set_shine(const float gloss, const float rim) {
    gloss_scale = std::clamp(gloss, 0.0f, 2.0f);
    rim_scale = std::clamp(rim, 0.0f, 2.0f);
    const auto scaled = [](const unsigned base, const float scale) {
        return std::min(255U, static_cast<unsigned>(std::lround(static_cast<float>(base) * scale)));
    };
    gloss_alpha = scaled(gloss_base, gloss_scale);
    rim_top_alpha = scaled(rim_top_base, rim_scale);
    rim_bottom_alpha = scaled(rim_bottom_base, rim_scale);
}

bool glass_enabled() { return glass_on; }

float current_glass() { return glass_transparency; }

bool is_dark() { return dark_mode; }

void set_glass(const float transparency) {
    glass_transparency = std::clamp(transparency, 0.0f, 0.9f);
    surface = glass_fill(glass_transparency);
}

ImU32 glass_fill(const float transparency) {
    if (!glass_on)
        return rgb(glass_tint);
    return rgb(glass_tint, static_cast<unsigned>(
                               std::lround((1.0f - std::clamp(transparency, 0.0f, 1.0f)) * 255.0f)));
}

void set_glass_enabled(const bool on) {
    glass_on = on;
    set_mode(dark_mode);
}

// Two full palettes, assigned wholesale so nothing is left half-updated.
// Both keep the same roles and hues: the dark one has light fills with dark
// ink on them, the light one deep fills with white ink. Solid by default;
// with glass on, cards, wells, borders, keys and rows are alpha over what
// is behind them.
void set_mode(const bool dark) {
    dark_mode = dark;
    if (dark) {
        dim_base = 0x000000;
        accent2 = rgb(0x4FD1C5);
        accent2_soft = rgb(0x4FD1C5, 36);
        accent2_line = rgb(0x4FD1C5, 140);
        accent2_text = rgb(0x8FE4DA);

        success = rgb(0x3FB950);
        warning = rgb(0xD29922);
        danger = rgb(0xF85149);
        danger_soft = rgb(0xF85149, 36);

        check_mark = rgb(0xFFFFFF);

        if (glass_on) {
            background = rgb(0x14131A);
            glass_tint = 0x201F26;
            surface_hi = rgb(0xFFFFFF, 14);
            well = rgb(0x000000, 96);
            well_hover = rgb(0x000000, 64);
            field = rgb(0xFFFFFF, 24);
            field_hover = rgb(0xFFFFFF, 40);
            field_active = rgb(0xFFFFFF, 56);
            popup_fill = rgb(glass_tint);
            sheen = rgb(0xFFFFFF, 40);
            gloss_base = 20;
            rim_top_base = 80;
            rim_bottom_base = 14;
            border = rgb(0xFFFFFF, 26);
            border_strong = rgb(0xFFFFFF, 64);
            text = rgb(0xECEAF4);
            text_dim = rgb(0xB8B5C6);
            text_faint = rgb(0x9D9AAD);
            scrollbar_hover = rgb(0xFFFFFF, 96);
            scrollbar_active = rgb(0xFFFFFF, 128);
        } else {
            background = rgb(0x000000);
            glass_tint = 0x161B22;
            surface_hi = rgb(0x21262D);
            well = rgb(0x0D1117);
            well_hover = rgb(0x161B22);
            field = rgb(0x21262D);
            field_hover = rgb(0x30363D);
            field_active = rgb(0x38414D);
            popup_fill = rgb(0x1C2128);
            sheen = rgb(0x000000, 0);
            gloss_base = 0;
            rim_top_base = 0;
            rim_bottom_base = 0;
            border = rgb(0x30363D);
            border_strong = rgb(0x484F58);
            text = rgb(0xF0F6FC);
            text_dim = rgb(0x9DA7B3);
            text_faint = rgb(0x828994);
            scrollbar_hover = rgb(0x484F58);
            scrollbar_active = rgb(0x6E7681);
        }
    } else {
        dim_base = 0xFFFFFF;
        accent2 = rgb(0x0B7F72);
        accent2_soft = rgb(0x0B7F72, 40);
        accent2_line = rgb(0x0B7F72, 170);
        accent2_text = rgb(0x086A5F);

        success = rgb(0x1A7F37);
        warning = rgb(0x9A6700);
        danger = rgb(0xCF222E);
        danger_soft = rgb(0xCF222E, 36);

        check_mark = rgb(0x1C1A26);

        if (glass_on) {
            background = rgb(0xECEAF3);
            glass_tint = 0xFFFFFF;
            surface_hi = rgb(0x2A2440, 14);
            well = rgb(0xFFFFFF, 225);
            well_hover = rgb(0xFFFFFF);
            field = rgb(0x2A2440, 26);
            field_hover = rgb(0x2A2440, 42);
            field_active = rgb(0x2A2440, 58);
            popup_fill = rgb(glass_tint);
            sheen = rgb(0xFFFFFF, 230);
            gloss_base = 110;
            rim_top_base = 255;
            rim_bottom_base = 90;
            border = rgb(0x2A2440, 30);
            border_strong = rgb(0x2A2440, 84);
            text = rgb(0x1C1A26);
            text_dim = rgb(0x4B485A);
            text_faint = rgb(0x5A5769);
            scrollbar_hover = rgb(0x2A2440, 110);
            scrollbar_active = rgb(0x2A2440, 140);
        } else {
            background = rgb(0xF6F8FA);
            glass_tint = 0xFFFFFF;
            surface_hi = rgb(0xF3F4F6);
            well = rgb(0xF6F8FA);
            well_hover = rgb(0xEAEEF2);
            field = rgb(0xEAEEF2);
            field_hover = rgb(0xDFE3E8);
            field_active = rgb(0xD0D7DE);
            popup_fill = rgb(0xFFFFFF);
            sheen = rgb(0x000000, 0);
            gloss_base = 0;
            rim_top_base = 0;
            rim_bottom_base = 0;
            border = rgb(0xD0D7DE);
            border_strong = rgb(0xAFB8C1);
            text = rgb(0x1F2328);
            text_dim = rgb(0x656D76);
            text_faint = rgb(0x57606A);
            scrollbar_hover = rgb(0xAFB8C1);
            scrollbar_active = rgb(0x8C959F);
        }
    }
    set_glass(glass_transparency);
    set_accent(accent_seed);
    set_shine(gloss_scale, rim_scale);
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
    style.PopupBorderSize = 0;
    style.FrameBorderSize = 0;
    style.WindowRounding = 0;
    style.ChildRounding = card_rounding;
    style.FrameRounding = std::min(8.0f, card_rounding);
    style.PopupRounding = card_rounding;
    style.ScrollbarRounding = 5;
    style.GrabRounding = 6;
    style.TabRounding = 8;
    style.SeparatorTextBorderSize = 1;
    style.SelectableTextAlign = ImVec2(0, 0.5f);
    style.DisabledAlpha = 0.4f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = vec(text);
    c[ImGuiCol_TextDisabled] = vec(text_faint);
    // The window and plain child windows are clear: the background (see
    // backdrop.hpp) and the cards (see ui::draw_glass()) paint themselves.
    c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    // Popups and tooltips are clear too: ui::glass_popups() paints them.
    c[ImGuiCol_PopupBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Border] = vec(border);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = vec(well);
    c[ImGuiCol_FrameBgHovered] = vec(well_hover);
    c[ImGuiCol_FrameBgActive] = vec(well_hover);
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
