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

} // namespace aoap
