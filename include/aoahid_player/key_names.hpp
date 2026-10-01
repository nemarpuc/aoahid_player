// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string_view>

namespace aoap {

// Looks up a keyboard key by name, ignoring case, and returns its HID usage
// on the Keyboard/Keypad page: "A".."Z", "Digit0".."Digit9", "F1".."F24",
// "Enter", "Esc", "Space", "Tab", "Backspace", the arrows, "Home"/"End"/
// "PageUp"/"PageDown", "Insert"/"Delete", and the modifiers ("LCtrl",
// "LShift", "LAlt", "LGui" and the R versions; "Ctrl"/"Shift"/"Alt"/"Gui"
// mean the left one). False when the name is unknown.
bool key_usage_from_name(std::string_view name, uint16_t& usage) noexcept;

// Consumer page (0x0C) usages the toggle profile declares; spec_builder.hpp's
// build_toggle() declares exactly this set.
inline constexpr uint16_t media_usages[] = {0x00B5, 0x00B6, 0x00B7, 0x00CD, 0x00E2, 0x00E9,
                                            0x00EA, 0x0223, 0x0224, 0x0238, 0x0201, 0x006F,
                                            0x0070};

bool media_usage_supported(uint16_t usage) noexcept;

// Looks up a media key by name, ignoring case: "Next", "Prev"/"Previous",
// "Stop", "PlayPause", "Mute", "VolumeUp", "VolumeDown", "Home" (AC Home),
// "Back" (AC Back), "New" (AC New), "BrightnessUp", "BrightnessDown". False
// when the name is unknown.
bool media_usage_from_name(std::string_view name, uint16_t& usage) noexcept;

} // namespace aoap
