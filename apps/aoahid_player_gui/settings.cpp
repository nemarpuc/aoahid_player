// SPDX-License-Identifier: MIT
#include "settings.hpp"

#include "aoahid_player/paths.hpp"
#include "aoahid_player/player.hpp"
#include "aoahid_player/spec_builder.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <system_error>

namespace gui {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

// Accepts decimal or 0x-prefixed hex; anything else leaves `out` alone.
void read_int(const std::string& text, const int minimum, const int maximum, int& out) {
    if (text.empty())
        return;
    errno = 0;
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 0);
    if (errno != 0 || end == text.c_str() || *end != '\0')
        return;
    if (value < minimum || value > maximum)
        return;
    out = static_cast<int>(value);
}

// HID usage codes are always entered and saved as 0x-prefixed hex (see the
// GUI's hex-only usage fields); a bare decimal number here is rejected
// instead of being silently reinterpreted as hex, since save_settings()
// never writes one.
void read_hex(const std::string& text, const int minimum, const int maximum, int& out) {
    if (text.size() < 3 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X'))
        return;
    errno = 0;
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 16);
    if (errno != 0 || end == text.c_str() || *end != '\0')
        return;
    if (value < minimum || value > maximum)
        return;
    out = static_cast<int>(value);
}

// Colours are six hex digits, RRGGBB, with no prefix.
void read_rgb(const std::string& text, int& out) {
    if (text.size() != 6 ||
        !std::all_of(text.begin(), text.end(),
                     [](const char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
        return;
    out = static_cast<int>(std::strtol(text.c_str(), nullptr, 16));
}

std::string rgb_text(const int color) {
    char text[8];
    std::snprintf(text, sizeof text, "%06X", static_cast<unsigned>(color) & 0xFFFFFFU);
    return text;
}

void read_float(const std::string& text, const float minimum, const float maximum, float& out) {
    if (text.empty())
        return;
    errno = 0;
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != '\0')
        return;
    if (value < minimum || value > maximum)
        return;
    out = value;
}

void read_bool(const std::string& text, bool& out) {
    if (text == "1" || text == "true" || text == "on")
        out = true;
    else if (text == "0" || text == "false" || text == "off")
        out = false;
}

void read_axes(const std::string& text, std::vector<aoahid_axis_role>& out) {
    std::vector<aoahid_axis_role> axes;
    std::stringstream stream(text);
    std::string piece;
    while (std::getline(stream, piece, ',')) {
        const aoap::spec_detail::AxisIdentity* identity =
            aoap::spec_detail::find_axis(trim(piece));
        if (identity == nullptr)
            return;
        if (std::find(axes.begin(), axes.end(), identity->role) != axes.end())
            return;
        axes.push_back(identity->role);
    }
    const bool has_x = std::find(axes.begin(), axes.end(), AOAHID_AXIS_X) != axes.end();
    const bool has_y = std::find(axes.begin(), axes.end(), AOAHID_AXIS_Y) != axes.end();
    if (has_x && has_y)
        out = std::move(axes);
}

std::string axes_text(const std::vector<aoahid_axis_role>& axes) {
    std::string text;
    for (const aoahid_axis_role role : axes) {
        const aoap::spec_detail::AxisIdentity* identity = aoap::spec_detail::find_axis(role);
        if (identity == nullptr)
            continue;
        if (!text.empty())
            text += ',';
        text += identity->name;
    }
    return text;
}

// A text value as it may be written: on one line. A line break in it (legal
// in a file name on Linux) would add lines of its own to the file, so such
// a value is left out.
std::string one_line(const std::string& value) {
    return value.find_first_of("\r\n") == std::string::npos ? value : std::string();
}

// Reads `key = value` lines over `settings`; lines it does not know, and
// values it cannot read, leave the setting as it was.
void read_lines(std::istream& file, Settings& settings) {
    std::string line;
    while (std::getline(file, line)) {
        const std::string_view view = trim(line);
        if (view.empty() || view.front() == '#')
            continue;
        const size_t equals = view.find('=');
        if (equals == std::string_view::npos)
            continue;
        const std::string key(trim(view.substr(0, equals)));
        const std::string value(trim(view.substr(equals + 1)));

        if (key == "touch")
            read_bool(value, settings.use_touch);
        else if (key == "touch.width")
            read_int(value, 1, INT32_MAX, settings.touch_width);
        else if (key == "touch.height")
            read_int(value, 1, INT32_MAX, settings.touch_height);
        else if (key == "touch.contacts")
            read_int(value, 1, 16, settings.touch_contacts);
        else if (key == "mouse")
            read_bool(value, settings.use_mouse);
        else if (key == "mouse.buttons")
            read_int(value, 1, 16, settings.mouse_buttons);
        else if (key == "keyboard")
            read_bool(value, settings.use_key);
        else if (key == "keyboard.usage_min")
            read_hex(value, 0x04, 0xDF, settings.key_min);
        else if (key == "keyboard.usage_max")
            read_hex(value, 0x04, 0xDF, settings.key_max);
        else if (key == "gamepad")
            read_bool(value, settings.use_gamepad);
        else if (key == "gamepad.buttons")
            read_int(value, 1, 32, settings.pad_buttons);
        else if (key == "gamepad.axis_bits")
            read_int(value, 2, 32, settings.pad_bits);
        else if (key == "gamepad.dpad") {
            if (value == "none")
                settings.pad_dpad = 0;
            else if (value == "hat")
                settings.pad_dpad = 1;
            else if (value == "buttons")
                settings.pad_dpad = 2;
        }
        else if (key == "gamepad.axes")
            read_axes(value, settings.pad_axes);
        else if (key == "record.coords") {
            if (value == "raw")
                settings.record_coords = 0;
            else if (value == "normalized")
                settings.record_coords = 1;
        }
        else if (key == "pen")
            read_bool(value, settings.use_pen);
        else if (key == "toggle")
            read_bool(value, settings.use_toggle);
        else if (key.rfind("adb_bridge.port.", 0) == 0 && key.size() > 16) {
            int port = 0;
            read_int(value, 1024, 65535, port);
            if (port != 0)
                settings.adb_ports[key.substr(16)] = port;
        }
        else if (key == "api.enabled")
            read_bool(value, settings.api_enabled);
        else if (key == "api.port")
            read_int(value, 1024, 65535, settings.api_port);
        else if (key == "pen.mode") {
            if (value == "direct")
                settings.pen_mode = 0;
            else if (value == "indirect")
                settings.pen_mode = 1;
        }
        else if (key == "live.release_key")
            read_int(value, 0, 348, settings.live_release_key);
        else if (key == "player.play_key")
            read_int(value, 0, 348, settings.player_play_key);
        else if (key == "player.stop_key")
            read_int(value, 0, 348, settings.player_stop_key);
        else if (key == "player.restart_key")
            read_int(value, 0, 348, settings.player_restart_key);
        else if (key == "ui.sidebar_width")
            read_float(value, 280.0f, 640.0f, settings.sidebar_width);
        else if (key == "ui.sidebar_collapsed")
            read_bool(value, settings.sidebar_collapsed);
        else if (key == "ui.dark_theme")
            read_bool(value, settings.dark_theme);
        else if (key == "ui.accent")
            read_rgb(value, settings.accent);
        else if (key == "ui.gloss")
            read_float(value, 0.0f, 2.0f, settings.gloss);
        else if (key == "ui.rim")
            read_float(value, 0.0f, 2.0f, settings.rim);
        else if (key == "ui.card_rounding")
            read_int(value, 0, 24, settings.card_rounding);
        else if (key == "ui.motion")
            read_bool(value, settings.motion);
        else if (key == "ui.scale") {
            float scale = settings.ui_scale;
            read_float(value, ui_scales[0], ui_scales[std::size(ui_scales) - 1], scale);
            settings.ui_scale = nearest_ui_scale(scale);
        }
        else if (key == "ui.sidebar_right")
            read_bool(value, settings.sidebar_right);
        else if (key == "ui.log_hidden")
            read_bool(value, settings.log_hidden);
        else if (key == "ui.toasts")
            read_bool(value, settings.toasts);
        else if (key == "ui.glass_on")
            read_bool(value, settings.glass_on);
        else if (key == "ui.glass")
            read_float(value, 0.0f, 0.9f, settings.glass);
        else if (key == "ui.bg_mode")
            read_int(value, 0, 2, settings.bg_mode);
        else if (key == "ui.bg_color")
            read_rgb(value, settings.bg_color);
        else if (key == "ui.bg_image")
            settings.bg_image = value;
        else if (key == "ui.bg_blur")
            read_int(value, 0, 40, settings.bg_blur);
        else if (key == "ui.bg_dim")
            read_float(value, 0.0f, 0.8f, settings.bg_dim);
        else if (key == "ui.bg_auto_dim")
            read_bool(value, settings.bg_auto_dim);
        else if (key == "window.x")
            read_int(value, -32768, 32767, settings.window_x);
        else if (key == "window.y")
            read_int(value, -32768, 32767, settings.window_y);
        else if (key == "window.width")
            read_int(value, 0, 16384, settings.window_width);
        else if (key == "window.height")
            read_int(value, 0, 16384, settings.window_height);
        else if (key == "ui.player_mode")
            read_int(value, 0, 1, settings.player_mode);
        else if (key == "ui.tab")
            read_int(value, 0, 5, settings.tab);
        else if (key == "ui.last_script")
            settings.last_script = value;
        else if (key == "ui.log_open")
            read_bool(value, settings.log_open);
        else if (key == "player.speed")
            read_float(value, static_cast<float>(aoap::Player::min_speed),
                       static_cast<float>(aoap::Player::max_speed), settings.speed);
        else if (key == "player.loop_limit")
            read_int(value, 0, INT32_MAX, settings.loop_limit);
        else if (key == "live.touch")
            read_bool(value, settings.live_touch);
        else if (key == "live.mouse")
            read_bool(value, settings.live_mouse);
        else if (key == "live.key")
            read_bool(value, settings.live_key);
        else if (key == "live.gamepad")
            read_bool(value, settings.live_gamepad);
        else if (key == "live.toggle")
            read_bool(value, settings.live_toggle);
        else if (key == "live.ratio_w")
            read_int(value, 0, INT32_MAX, settings.live_ratio_w);
        else if (key == "live.ratio_h")
            read_int(value, 0, INT32_MAX, settings.live_ratio_h);
        else if (key == "live.rotation")
            read_int(value, 0, 3, settings.live_rotation);
        else if (key == "live.phone_glass")
            read_bool(value, settings.live_phone_glass);
        else if (key == "live.phone_clear")
            read_float(value, 0.0f, 1.0f, settings.live_phone_clear);
        else if (key == "live.phone_rounding")
            read_int(value, 0, 60, settings.live_phone_rounding);
        else if (key == "live_image.path")
            settings.live_image_path = value;
        else if (key == "live_image.x")
            read_float(value, 0.0f, 1.0f, settings.live_image_x);
        else if (key == "live_image.y")
            read_float(value, 0.0f, 1.0f, settings.live_image_y);
        else if (key == "live_image.half_width")
            read_float(value, 0.02f, 4.0f, settings.live_image_half_width);
        else if (key == "live_image.rotation")
            read_float(value, -1000.0f, 1000.0f, settings.live_image_rotation);
        else if (key == "live_image.opacity")
            read_float(value, 0.0f, 1.0f, settings.live_image_opacity);
        else if (key == "live_image.locked")
            read_bool(value, settings.live_image_locked);
    }
    if (settings.key_max < settings.key_min)
        settings.key_max = settings.key_min;
}

// Everything about how the window looks: the part of the settings a theme
// preset (themes/<name>.theme) holds.
void write_look(std::ostream& out, const Settings& settings) {
    out << "ui.dark_theme = " << (settings.dark_theme ? 1 : 0) << '\n';
    out << "ui.accent = " << rgb_text(settings.accent) << '\n';
    out << "ui.glass_on = " << (settings.glass_on ? 1 : 0) << '\n';
    out << "ui.glass = " << settings.glass << '\n';
    out << "ui.gloss = " << settings.gloss << '\n';
    out << "ui.rim = " << settings.rim << '\n';
    out << "ui.card_rounding = " << settings.card_rounding << '\n';
    out << "ui.motion = " << (settings.motion ? 1 : 0) << '\n';
    out << "ui.scale = " << settings.ui_scale << '\n';
    out << "ui.bg_mode = " << settings.bg_mode << '\n';
    out << "ui.bg_color = " << rgb_text(settings.bg_color) << '\n';
    out << "ui.bg_image = " << one_line(settings.bg_image) << '\n';
    out << "ui.bg_blur = " << settings.bg_blur << '\n';
    out << "ui.bg_dim = " << settings.bg_dim << '\n';
    out << "ui.bg_auto_dim = " << (settings.bg_auto_dim ? 1 : 0) << '\n';
    out << "live.phone_glass = " << (settings.live_phone_glass ? 1 : 0) << '\n';
    out << "live.phone_clear = " << settings.live_phone_clear << '\n';
    out << "live.phone_rounding = " << settings.live_phone_rounding << '\n';
    out << "ui.sidebar_right = " << (settings.sidebar_right ? 1 : 0) << '\n';
    out << "ui.log_hidden = " << (settings.log_hidden ? 1 : 0) << '\n';
    out << "ui.toasts = " << (settings.toasts ? 1 : 0) << '\n';
}

// Written to a temporary file and renamed over the old one, so a crash never
// leaves a half-written file.
bool write_file(const std::filesystem::path& path, const std::string& text, std::string& error) {
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "cannot write " + aoap::path_utf8(temporary);
            return false;
        }
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.flush();
        if (!file) {
            error = "cannot write " + aoap::path_utf8(temporary);
            return false;
        }
    }
    std::error_code code;
    std::filesystem::rename(temporary, path, code);
    if (code) {
        std::filesystem::remove(temporary, code);
        error = "cannot replace " + aoap::path_utf8(path);
        return false;
    }
    return true;
}

constexpr const char* theme_extension = ".theme";

std::filesystem::path theme_file(const std::string& name) {
    return theme_directory() / aoap::utf8_path(name + theme_extension);
}

} // namespace

float nearest_ui_scale(const float scale) {
    float best = ui_scales[0];
    for (const float candidate : ui_scales)
        if (std::abs(candidate - scale) < std::abs(best - scale))
            best = candidate;
    return best;
}

std::filesystem::path settings_path() {
    return aoap::utf8_path(aoap::executable_directory()) / "aoahid_player_gui.ini";
}

Settings load_settings(const std::filesystem::path& path) {
    Settings settings;
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return settings;

    read_lines(file, settings);
    return settings;
}

bool save_settings(const std::filesystem::path& path, const Settings& settings,
                   std::string& error) {
    std::ostringstream out;
    char hex[8];
    out << "# AOA HID Player settings, saved automatically. Delete this file to reset.\n";
    out << "touch = " << (settings.use_touch ? 1 : 0) << '\n';
    out << "touch.width = " << settings.touch_width << '\n';
    out << "touch.height = " << settings.touch_height << '\n';
    out << "touch.contacts = " << settings.touch_contacts << '\n';
    out << "mouse = " << (settings.use_mouse ? 1 : 0) << '\n';
    out << "mouse.buttons = " << settings.mouse_buttons << '\n';
    out << "keyboard = " << (settings.use_key ? 1 : 0) << '\n';
    std::snprintf(hex, sizeof hex, "0x%02X", static_cast<unsigned>(settings.key_min));
    out << "keyboard.usage_min = " << hex << '\n';
    std::snprintf(hex, sizeof hex, "0x%02X", static_cast<unsigned>(settings.key_max));
    out << "keyboard.usage_max = " << hex << '\n';
    out << "gamepad = " << (settings.use_gamepad ? 1 : 0) << '\n';
    out << "gamepad.buttons = " << settings.pad_buttons << '\n';
    out << "gamepad.axis_bits = " << settings.pad_bits << '\n';
    out << "gamepad.dpad = "
        << (settings.pad_dpad == 0 ? "none" : settings.pad_dpad == 2 ? "buttons" : "hat") << '\n';
    out << "gamepad.axes = " << axes_text(settings.pad_axes) << '\n';
    out << "pen = " << (settings.use_pen ? 1 : 0) << '\n';
    out << "pen.mode = " << (settings.pen_mode == 1 ? "indirect" : "direct") << '\n';
    out << "toggle = " << (settings.use_toggle ? 1 : 0) << '\n';
    for (const auto& [device, port] : settings.adb_ports)
        if (!one_line(device).empty())
            out << "adb_bridge.port." << device << " = " << port << '\n';
    out << "api.enabled = " << (settings.api_enabled ? 1 : 0) << '\n';
    out << "api.port = " << settings.api_port << '\n';
    out << "live.release_key = " << settings.live_release_key << '\n';
    out << "player.play_key = " << settings.player_play_key << '\n';
    out << "player.stop_key = " << settings.player_stop_key << '\n';
    out << "player.restart_key = " << settings.player_restart_key << '\n';
    out << "ui.sidebar_width = " << settings.sidebar_width << '\n';
    out << "ui.sidebar_collapsed = " << (settings.sidebar_collapsed ? 1 : 0) << '\n';
    out << "window.x = " << settings.window_x << '\n';
    out << "window.y = " << settings.window_y << '\n';
    out << "window.width = " << settings.window_width << '\n';
    out << "window.height = " << settings.window_height << '\n';
    out << "ui.tab = " << settings.tab << '\n';
    out << "ui.player_mode = " << settings.player_mode << '\n';
    out << "ui.last_script = " << one_line(settings.last_script) << '\n';
    out << "ui.log_open = " << (settings.log_open ? 1 : 0) << '\n';
    out << "player.speed = " << settings.speed << '\n';
    out << "player.loop_limit = " << settings.loop_limit << '\n';
    out << "live.touch = " << (settings.live_touch ? 1 : 0) << '\n';
    out << "live.mouse = " << (settings.live_mouse ? 1 : 0) << '\n';
    out << "live.key = " << (settings.live_key ? 1 : 0) << '\n';
    out << "live.gamepad = " << (settings.live_gamepad ? 1 : 0) << '\n';
    out << "live.toggle = " << (settings.live_toggle ? 1 : 0) << '\n';
    out << "live.ratio_w = " << settings.live_ratio_w << '\n';
    out << "live.ratio_h = " << settings.live_ratio_h << '\n';
    out << "record.coords = " << (settings.record_coords == 1 ? "normalized" : "raw") << '\n';
    out << "live.rotation = " << settings.live_rotation << '\n';
    out << "live_image.path = " << one_line(settings.live_image_path) << '\n';
    out << "live_image.x = " << settings.live_image_x << '\n';
    out << "live_image.y = " << settings.live_image_y << '\n';
    out << "live_image.half_width = " << settings.live_image_half_width << '\n';
    out << "live_image.rotation = " << settings.live_image_rotation << '\n';
    out << "live_image.opacity = " << settings.live_image_opacity << '\n';
    out << "live_image.locked = " << (settings.live_image_locked ? 1 : 0) << '\n';
    write_look(out, settings);
    return write_file(path, out.str(), error);
}

std::filesystem::path theme_directory() {
    return aoap::utf8_path(aoap::executable_directory()) / "themes";
}

std::vector<std::string> list_themes() {
    std::vector<std::string> names;
    std::error_code code;
    for (const auto& item : std::filesystem::directory_iterator(theme_directory(), code)) {
        if (item.is_regular_file(code) && item.path().extension() == theme_extension)
            names.push_back(aoap::path_utf8(item.path().stem()));
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string theme_name_problem(const std::string& name) {
    if (name.empty())
        return "Type a name.";
    if (name.front() == '.' || name.front() == ' ' || name.back() == ' ')
        return "A name cannot start with a dot or start or end with a space.";
    if (name.find_first_of("/\\:*?\"<>|") != std::string::npos)
        return "A name cannot contain / \\ : * ? \" < > |.";
    if (std::any_of(name.begin(), name.end(),
                    [](const char c) { return static_cast<unsigned char>(c) < 0x20; }))
        return "A name cannot contain control characters.";
    if (name.size() > 100)
        return "A name can be at most 100 bytes.";
    // Device names Windows reserves, with or without an extension.
    std::string upper = name.substr(0, name.find('.'));
    for (char& c : upper)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    static const char* const reserved[] = {"CON", "PRN", "AUX", "NUL"};
    const bool numbered = upper.size() == 4 && (upper.rfind("COM", 0) == 0 ||
                                                 upper.rfind("LPT", 0) == 0) &&
                          upper[3] >= '1' && upper[3] <= '9';
    if (numbered || std::find(std::begin(reserved), std::end(reserved), upper) !=
                        std::end(reserved))
        return "That name is reserved on Windows.";
    return {};
}

bool save_theme(const std::string& name, const Settings& settings, std::string& error) {
    error = theme_name_problem(name);
    if (!error.empty())
        return false;
    std::error_code code;
    std::filesystem::create_directories(theme_directory(), code);
    std::ostringstream out;
    out << "# AOA HID Player look preset.\n";
    write_look(out, settings);
    return write_file(theme_file(name), out.str(), error);
}

bool load_theme(const std::string& name, Settings& settings, std::string& error) {
    error = theme_name_problem(name);
    if (!error.empty())
        return false;
    std::ifstream file(theme_file(name), std::ios::binary);
    if (!file) {
        error = "cannot open the preset \"" + name + "\"";
        return false;
    }
    read_lines(file, settings);
    return true;
}

bool delete_theme(const std::string& name, std::string& error) {
    error = theme_name_problem(name);
    if (!error.empty())
        return false;
    std::error_code code;
    if (!std::filesystem::remove(theme_file(name), code) || code) {
        error = "cannot delete the preset \"" + name + "\"";
        return false;
    }
    return true;
}

} // namespace gui
