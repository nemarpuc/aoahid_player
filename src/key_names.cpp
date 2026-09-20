// SPDX-License-Identifier: MIT
#include "aoahid_player/key_names.hpp"

#include <array>

namespace aoap {
namespace {

struct Named {
    std::string_view name; // lowercase
    uint16_t usage;
};

constexpr std::array<Named, 52> names{{
    {"enter", 0x28},     {"return", 0x28},   {"esc", 0x29},        {"escape", 0x29},
    {"backspace", 0x2A}, {"tab", 0x2B},      {"space", 0x2C},      {"minus", 0x2D},
    {"equal", 0x2E},     {"lbracket", 0x2F}, {"rbracket", 0x30},   {"backslash", 0x31},
    {"semicolon", 0x33}, {"quote", 0x34},    {"grave", 0x35},      {"comma", 0x36},
    {"period", 0x37},    {"slash", 0x38},    {"capslock", 0x39},   {"printscreen", 0x46},
    {"scrolllock", 0x47}, {"pause", 0x48},   {"insert", 0x49},     {"home", 0x4A},
    {"pageup", 0x4B},    {"delete", 0x4C},   {"end", 0x4D},        {"pagedown", 0x4E},
    {"right", 0x4F},     {"left", 0x50},     {"down", 0x51},       {"up", 0x52},
    {"numlock", 0x53},   {"lctrl", 0xE0},    {"lshift", 0xE1},     {"lalt", 0xE2},
    {"lgui", 0xE3},      {"rctrl", 0xE4},    {"rshift", 0xE5},     {"ralt", 0xE6},
    {"rgui", 0xE7},      {"ctrl", 0xE0},     {"control", 0xE0},    {"shift", 0xE1},
    {"alt", 0xE2},       {"gui", 0xE3},      {"win", 0xE3},        {"meta", 0xE3},
    {"super", 0xE3},     {"lwin", 0xE3},     {"rwin", 0xE7},       {"menu", 0x65},
}};

char lower(const char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c | 0x20) : c; }

bool same(const std::string_view a, const std::string_view lowered) noexcept {
    if (a.size() != lowered.size())
        return false;
    for (size_t index = 0; index < a.size(); ++index) {
        if (lower(a[index]) != lowered[index])
            return false;
    }
    return true;
}

// "1".."24" after a leading 'f'.
bool function_key(const std::string_view name, uint16_t& usage) noexcept {
    if (name.size() < 2 || name.size() > 3 || lower(name[0]) != 'f')
        return false;
    unsigned number = 0;
    for (size_t index = 1; index < name.size(); ++index) {
        if (name[index] < '0' || name[index] > '9')
            return false;
        number = number * 10U + static_cast<unsigned>(name[index] - '0');
    }
    if (name[1] == '0' || number < 1 || number > 24)
        return false;
    usage = static_cast<uint16_t>(number <= 12 ? 0x3A + (number - 1) : 0x68 + (number - 13));
    return true;
}

} // namespace

bool key_usage_from_name(const std::string_view name, uint16_t& usage) noexcept {
    if (name.size() == 1) {
        const char c = lower(name[0]);
        if (c >= 'a' && c <= 'z') {
            usage = static_cast<uint16_t>(0x04 + (c - 'a'));
            return true;
        }
        return false;
    }
    constexpr std::string_view digit = "digit";
    if (name.size() == digit.size() + 1 && same(name.substr(0, digit.size()), digit) &&
        name.back() >= '0' && name.back() <= '9') {
        usage = name.back() == '0' ? uint16_t{0x27}
                                   : static_cast<uint16_t>(0x1E + (name.back() - '1'));
        return true;
    }
    if (function_key(name, usage))
        return true;
    for (const Named& entry : names) {
        if (same(name, entry.name)) {
            usage = entry.usage;
            return true;
        }
    }
    return false;
}

} // namespace aoap
