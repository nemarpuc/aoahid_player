// SPDX-License-Identifier: MIT
#pragma once

#include <imgui.h>

namespace gui::theme {

// Flat palette: neutral surfaces in a few steps, one signal accent for what
// can be pressed or is selected, a second accent for what has been picked,
// and muted status colours. No gradients, glows, or shadows. Values below
// are runtime variables, not constants: set_mode() rewrites every one of
// them when the theme switches between dark and light (see its definition
// in theme.cpp for both palettes side by side).
inline constexpr ImU32 rgb(const unsigned hex, const unsigned alpha = 255U) {
    return IM_COL32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, alpha);
}

inline ImU32 background;
inline ImU32 surface;    // cards
inline ImU32 surface_hi; // rows inside cards
inline ImU32 field;      // inputs, tracks, quiet buttons
inline ImU32 field_hover;
inline ImU32 field_active;
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
// recording): purple in the dark theme, teal in the light theme — each
// tuned so `accent_ink` reads clearly as text drawn on top of the fill.
inline ImU32 accent;
inline ImU32 accent_hover;
inline ImU32 accent_active;
inline ImU32 accent_text; // accent used as free-standing text
inline ImU32 accent_soft;
inline ImU32 accent_line;
inline ImU32 accent_ink; // text drawn on top of an `accent` fill

// `accent2` is "this is what you picked" (a selected device, a chosen
// script, which tab is open, a search hit): the other of the two colours
// from `accent` in each theme, never a gradient between them.
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
inline ImU32 popup_bg;
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

// Loads the embedded font and applies the style for `dpi_scale`. set_mode()
// picks the palette beforehand; this only reads it.
void setup(float dpi_scale);
// Re-applies the style, e.g. after the window moved to a monitor with
// another scale, or after set_mode() changed the palette.
void apply_style(float dpi_scale);

} // namespace gui::theme
