// SPDX-License-Identifier: MIT
#pragma once

#include <aoahid.h>

#include <filesystem>
#include <string>
#include <vector>

namespace gui {

// What the GUI remembers between runs: the profile choices and the adb
// option. Defaults are what a first run shows.
struct Settings {
    bool use_touch{true};
    int touch_width{1080};
    int touch_height{2400};
    int touch_contacts{10};
    bool use_mouse{};
    int mouse_buttons{5};
    bool use_key{};
    int key_min{0x04};
    int key_max{0x65};
    bool use_gamepad{};
    int pad_buttons{16};
    int pad_bits{16};
    int pad_dpad{1}; // 0 none, 1 hat, 2 buttons
    std::vector<aoahid_axis_role> pad_axes{AOAHID_AXIS_X, AOAHID_AXIS_Y, AOAHID_AXIS_Z,
                                           AOAHID_AXIS_RZ};
    bool use_pen{};
    int pen_mode{}; // 0 direct screen, 1 indirect tablet
    // The GLFW key that releases the captured pointer in Live control's
    // mouse mode. 0 means "not set", which live control treats as Escape.
    int live_release_key{};
    // The GLFW key that toggles the Live tab's real full screen mode. 0
    // means "not set", which is treated as F11. Escape always exits full
    // screen too, regardless of this setting.
    int live_fullscreen_key{};
    // GLFW keys for the Player tab's play/pause, stop, and back-to-start
    // actions. 0 means "not set": Space, Escape (or Ctrl+S), and Home (or
    // Backspace) respectively.
    int player_play_key{};
    int player_stop_key{};
    int player_restart_key{};
    // Unscaled width of the sidebar, dragged via the splitter next to it.
    float sidebar_width{392.0f};
    // Whether Devices/Profiles/Connect are collapsed to a slim rail.
    bool sidebar_collapsed{};
    // Dark theme (purple accent) or light theme (teal accent).
    bool dark_theme{true};

    // Window geometry in OS pixels. 0 width/height means "unset": main.cpp
    // then falls back to its own centred, monitor-fitted default instead of
    // trying to place a zero-sized window.
    int window_x{};
    int window_y{};
    int window_width{};
    int window_height{};

    // The Recorder tab's coordinate mode (aoap::CoordMode's order): 0 raw,
    // 1 normalized.
    int record_coords{};

    // 0 = Player, 1 = Live, 2 = Playlist, 3 = Recorder (App::Tab's order).
    int tab{};
    // A script reference (see script_reference()/resolve_script_reference()
    // in playlist.hpp), or empty to fall back to the first script found.
    std::string last_script;
    float speed{1.0f};
    int loop_limit{}; // 0 = repeats until stopped
    bool log_open{true};

    // The Live tab's forwarding toggles.
    bool live_touch{true};
    bool live_mouse{};
    bool live_key{};
    bool live_gamepad{};

    // The Live preview's shape and orientation; 0/0 ratio follows the
    // connected touchscreen. See App::live_ratio_w_/h_/live_rotation_.
    int live_ratio_w{};
    int live_ratio_h{};
    int live_rotation{};

    // The Live tab's optional reference image overlay; see
    // LiveImageOverlay::State. Empty path means none was loaded.
    std::string live_image_path;
    float live_image_x{0.5f}; // centre, as a fraction of the preview
    float live_image_y{0.5f};
    float live_image_half_width{0.35f}; // fraction of the preview's width
    float live_image_rotation{0.0f};    // radians
    float live_image_opacity{1.0f};

    bool operator==(const Settings&) const = default;
};

// aoahid_player_gui.ini next to the executable, so the folder stays portable.
std::filesystem::path settings_path();

// A missing file gives the defaults. A value that cannot be read or is out
// of range keeps its default, so a hand-edited file never breaks startup.
Settings load_settings(const std::filesystem::path& path);

// Written to a temporary file and renamed over the old one, so a crash never
// leaves a half-written file.
bool save_settings(const std::filesystem::path& path, const Settings& settings,
                   std::string& error);

} // namespace gui
