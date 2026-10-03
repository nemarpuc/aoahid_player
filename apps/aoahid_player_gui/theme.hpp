// SPDX-License-Identifier: MIT
#pragma once

#include <imgui.h>

namespace gui::theme {

// Cards on a plain background, wells sunk into them for what can be typed,
// keys raised from them for what can be pressed, one signal accent for what
// is happening, a second accent for what has been picked, and status
// colours. Solid by default; with glass on, the cards are translucent with a
// hairline edge and a sheen line along the top.
// The phone preview is the only true black. No gradients or shadows; the
// only blur is a background picture's, precomputed (see backdrop.hpp).
// Values below are runtime variables, not constants: set_mode() rewrites
// every one of them when the theme switches between dark and light (see its
// definition in theme.cpp for both palettes side by side).
inline constexpr ImU32 rgb(const unsigned hex, const unsigned alpha = 255U) {
    return IM_COL32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, alpha);
}

inline ImU32 background;
inline ImU32 surface;    // cards: `glass_tint` at the opacity set_glass() chose
inline ImU32 surface_hi; // rows inside cards
inline ImU32 well;       // text and number inputs: past the card, away from `field`
inline ImU32 well_hover;
inline ImU32 field; // keys: buttons and tracks, raised from the card
inline ImU32 field_hover;
inline ImU32 field_active;
inline ImU32 popup_fill; // popups and tooltips when glass is off
inline ImU32 sheen; // the glossy top line on cards and keys
// Glass cards: a white gloss over their top part, fading to nothing, and a
// white rim brighter at the top than at the bottom. Alphas, 0 to 255.
inline unsigned gloss_alpha;
inline unsigned rim_top_alpha;
inline unsigned rim_bottom_alpha;
// What a background picture is dimmed towards: black in the dark theme,
// white in the light one.
inline unsigned dim_base;
inline ImU32 border;
inline ImU32 border_strong;
inline ImU32 text;
inline ImU32 text_dim;
// text_faint on `background` stays at or above 4.5:1 (WCAG AA for normal
// text) in both palettes; it carries real information (device VID:PID/
// serial, subtitles), not just decoration, so it stays readable even though
// it is dimmer than text_dim.
inline ImU32 text_faint;

// `accent` is "this is happening right now" (playing, connecting,
// recording): the colour chosen on the Settings tab (light purple unless
// changed), deepened in the light theme; see set_accent(). `accent_ink`
// always reads clearly as text drawn on top of the fill.
inline ImU32 accent;
inline ImU32 accent_hover;
inline ImU32 accent_active;
inline ImU32 accent_text; // accent used as free-standing text
inline ImU32 accent_soft;
inline ImU32 accent_line;
inline ImU32 accent_ink; // text drawn on top of an `accent` fill

// `accent2` is "this is what you picked" (a selected device, a chosen
// script, which tab is open, a search hit): teal in both themes, never a
// gradient between it and `accent`.
inline ImU32 accent2;
inline ImU32 accent2_soft;
inline ImU32 accent2_line;
inline ImU32 accent2_text; // accent2 used as free-standing text

inline ImU32 success;
inline ImU32 warning;
inline ImU32 danger;
inline ImU32 danger_soft;

// A few more surfaces that also flip with the mode but have no other use
// outside apply_style().
inline ImU32 scrollbar_hover;
inline ImU32 scrollbar_active;
inline ImU32 check_mark;       // ImGui's own Checkbox/RadioButton glyph
inline ImU32 text_selected_bg; // selected text inside a text field

inline ImVec4 vec(const ImU32 color) { return ImGui::ColorConvertU32ToFloat4(color); }

// Font sizes in unscaled pixels; DPI scaling is applied by ImGui. Roboto's
// digits are tabular, so running clocks and counters do not jitter.
inline constexpr float font_body = 15.0f;
inline constexpr float font_small = 13.0f;
inline constexpr float font_title = 15.5f;
inline constexpr float font_display = 30.0f;

// Overwrites every colour above with the dark or light palette. Call before
// setup()/apply_style() so they build the ImGui style from the right values.
void set_mode(bool dark);
// How much the cards let the background through, 0 (opaque) to 0.9;
// rewrites `surface`. Kept across set_mode().
void set_glass(float transparency);
// Glass on: translucent cards, popups and keys with a gloss and a lit rim.
// Off (the default): the same layout in solid colours. Kept across
// set_mode(), and rewrites the palette like it.
void set_glass_enabled(bool on);
[[nodiscard]] bool glass_enabled();
// The transparency set_glass() last chose (0 opaque to 0.9), and whether the
// dark palette is active; keys that float over the glass follow both.
[[nodiscard]] float current_glass();
[[nodiscard]] bool is_dark();
// Every accent shade from one colour (0xRRGGBB): used as is in the dark
// theme, made more vivid and deep enough for white text in the light one.
// Kept across set_mode().
void set_accent(unsigned seed);
// Scales the gloss and rim strengths (each 0 to 2, 1 = the theme's own).
// Kept across set_mode().
void set_shine(float gloss, float rim);
// Cards' corner radius in unscaled pixels; apply_style() reads it.
inline float card_rounding = 12.0f;
// The glass fill at any transparency, 0 (opaque) to 1 (clear), for glass
// that is not a card (the Live phone screen).
[[nodiscard]] ImU32 glass_fill(float transparency);

// Loads the embedded font and applies the style for `dpi_scale`. set_mode()
// picks the palette beforehand; this only reads it.
void setup(float dpi_scale);
// Re-applies the style, e.g. after the window moved to a monitor with
// another scale, or after set_mode() changed the palette.
void apply_style(float dpi_scale);

} // namespace gui::theme
