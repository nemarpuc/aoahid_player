// SPDX-License-Identifier: MIT
#pragma once

#include <imgui.h>

#include <string>

// The window's background: a plain colour, or a picture covering the whole
// window with a blurred copy that glass cards show through. The blur is
// computed once, on a small copy, when the picture or the blur radius
// changes, so a frame only draws a few textured quads. One per process,
// used from the UI thread only.
namespace gui::backdrop {

// Decodes `path`, replacing the current picture. Returns an empty string on
// success, or a message to show the user; the old picture stays on failure.
[[nodiscard]] std::string load(const std::string& path);
void clear();
[[nodiscard]] bool loaded();
// The loaded picture's path, empty when none is loaded.
[[nodiscard]] const std::string& path();

// Blur radius in window pixels, 0 to 40; recomputes the blurred copy.
void set_blur(int radius);

// Fills `min`..`max` (the whole window) for this frame: the picture when
// `image` is set and one is loaded, darkened (or, in the light theme,
// lightened) by `dim` from 0 to 1, else `color`.
void draw(ImDrawList* list, ImVec2 min, ImVec2 max, bool image, ImU32 color, float dim);

// The blurred picture under a card at `p0`..`p1`, lined up with what draw()
// showed this frame and dimmed the same way. Nothing when draw() showed no
// picture.
void draw_frosted(ImDrawList* list, ImVec2 p0, ImVec2 p1, float rounding);

// An opaque base for glass that floats over other content (popups,
// tooltips), so nothing under it shows through: the blurred picture when
// draw() showed one this frame, else the colour it filled the window with.
void draw_base(ImDrawList* list, ImVec2 p0, ImVec2 p1, float rounding);

} // namespace gui::backdrop
