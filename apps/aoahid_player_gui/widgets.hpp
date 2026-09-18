// SPDX-License-Identifier: MIT
#pragma once

#include <imgui.h>

#include <cstdint>
#include <string>

namespace gui::ui {

enum class Icon {
    play,
    pause,
    stop,
    restart,
    record,
    refresh,
    chevron_down,
    chevron_right,
    close,
    up,
    down,
    check,
    search,
    trash,
    expand,
    collapse,
    monitor, // a screen with a small stand, for "Live"
    list,    // three lines, for "Playlist"
    sun      // a small circle with rays, for the light/dark theme toggle
};

// What a button is for, which decides how loud it looks.
enum class Tone {
    primary,   // the one main action of a card: solid accent
    secondary, // everyday actions: quiet filled
    quiet,     // toolbar icons: no fill until hovered
    danger,    // destructive but routine (disconnect): quiet with red text
    record,    // starts a recording: quiet filled with a red record dot
};

// Unscaled pixels to current pixels, following the font's DPI scale.
float px(float value);

// Buttons, tabs, and similar ease their fill color (and the tab pill its
// position) toward each new state instead of snapping, so hovering and
// switching tabs read as motion. Call begin_frame_animations() once, before
// drawing, and check animations_active() afterward to decide whether to
// redraw again next frame instead of waiting idle for the next input event.
void begin_frame_animations();
[[nodiscard]] bool animations_active();

// A flat card with an optional title. Height 0 fits the contents; negative
// fills the remaining space minus that much.
bool begin_card(const char* id, const char* title = nullptr, float height = 0.0f);
void end_card();

// Card heading, aligned to a frame-height row so controls can sit beside it.
void card_title(const char* text);
// Small dim label above a control.
void caption(const char* text);
void text_dim(const char* format, ...) IM_FMTARGS(1);
void text_colored(ImU32 color, const char* format, ...) IM_FMTARGS(2);

// Width 0 fits the label, negative fills the row minus that much; height 0
// uses the frame height.
bool button(const char* label, ImVec2 size = ImVec2(0, 0), Tone tone = Tone::secondary);
// A round button with a drawn icon.
bool icon_button(const char* id, Icon icon, float diameter, Tone tone,
                 const char* tooltip = nullptr);

// A search field with a magnifier and, once there is text, a clear button.
// Esc also clears it; `focus` puts the caret in it. Returns true on Enter.
// `active` reports whether the field is being typed in.
bool search_box(const char* id, std::string* text, float width, const char* hint, bool focus,
                bool* active = nullptr);

// True on the frame Escape goes down, even while a text field holds it.
[[nodiscard]] bool escape_pressed();

// Switch with a label on its right.
bool toggle(const char* label, bool* value);

void draw_icon(ImDrawList* list, Icon icon, ImVec2 center, float size, ImU32 color);
// A check box drawn at `p0`, for custom rows.
void draw_check(ImDrawList* list, ImVec2 p0, float size, bool checked, ImU32 fill = 0);

// Draws `text` at `pos` in `color`, truncating with a trailing "..." if it
// would exceed `max_width`. For raw ImDrawList text (device names, serials)
// that shares a row with other content, so it can never draw under it.
void draw_text_ellipsized(ImDrawList* list, ImVec2 pos, ImU32 color, const char* text,
                          float max_width);

// A dense status readout: a small square dot and its label, with no pill
// background — several of these separated by status_divider() read as one
// hardware-panel-style status line. `blink` fades the dot (recording).
void status_item(const char* text, ImU32 dot, bool blink = false);
[[nodiscard]] float status_item_width(const char* text);
void status_divider();

void spinner(float radius, ImU32 color);

// A seekable progress bar. While dragging, `preview` follows the pointer;
// returns true on the frame the pointer is released, with `preview` set to
// the chosen fraction. `fill` colors the filled portion and its knob accent
// (theme::accent if omitted), so callers can tell one bar from another.
struct ScrubState {
    bool hovered{};
    bool dragging{};
    float hover_fraction{};
};
bool scrub_bar(const char* id, float fraction, float width, float height, float* preview,
               ScrubState* state = nullptr, ImU32 fill = 0);

// A thin track with a round knob and the value printed to its right, filling
// the rest of the row, e.g. "10 fingers" or "1.50x". Click or drag to set;
// the value snaps to `step`. Returns true while the value changes.
bool slider(const char* id, float* value, float minimum, float maximum, int decimals,
            const char* unit, float step = 0.0f, bool logarithmic = false);
bool slider_int(const char* id, int* value, int minimum, int maximum, const char* unit);

// Right-aligns the next item of `width` in the current line.
void align_right(float width);

} // namespace gui::ui
