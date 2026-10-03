// SPDX-License-Identifier: MIT
#pragma once

#include <aoahid.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace gui {

// The UI scale choices (Small, Normal, Large, Larger), multiplied into the
// display's own DPI scale.
inline constexpr float ui_scales[] = {0.85f, 1.0f, 1.15f, 1.3f};
[[nodiscard]] float nearest_ui_scale(float scale);

// What the GUI remembers between runs: the profile choices and the adb
// option. Defaults are what a first run shows.
struct Settings {
    bool use_touch{true};
    int touch_width{1440};
    int touch_height{2560};
    int touch_contacts{16};
    bool use_mouse{true};
    int mouse_buttons{5};
    bool use_key{true};
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
    bool use_toggle{};

    // The ADB tab's bridge port per device key (aoap::DeviceEntry::key).
    std::map<std::string, int> adb_ports;
    // The local HTTP control API (see control_api.hpp). Off by default;
    // binds 127.0.0.1 only.
    bool api_enabled{};
    int api_port{47821};
    // The GLFW key that releases the captured pointer in Live control's
    // mouse mode. 0 means "not set", which live control treats as Escape.
    int live_release_key{};
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
    // Dark theme or light theme.
    bool dark_theme{true};
    // The accent colour every accent shade is made from (0xRRGGBB).
    int accent{0x388BFD};
    // Glass look on (translucent cards, gloss) or the solid look.
    bool glass_on{false};
    // With glass on, how much the cards let the background through, 0
    // (opaque) to 0.9.
    float glass{0.5f};
    // The glass gloss and rim strengths, 0 to 2 (1 = the theme's own), and
    // the cards' corner radius in unscaled pixels, 0 to 24.
    float gloss{1.0f};
    float rim{1.0f};
    int card_rounding{12};
    // A short fade when the tab changes.
    bool motion{true};
    // One of ui_scales.
    float ui_scale{1.0f};
    // Layout: the sidebar on the right instead of the left, the Activity
    // panel hidden, and notifications for problems and the connection.
    bool sidebar_right{};
    bool log_hidden{};
    bool toasts{true};
    // The window's background: 0 the theme's colour, 1 `bg_color`, 2 the
    // picture at `bg_image` (blurred by `bg_blur` window pixels behind the
    // cards, and dimmed by `bg_dim` from 0 to 0.8).
    int bg_mode{0};
    int bg_color{0x000000};
    std::string bg_image;
    int bg_blur{1};
    float bg_dim{0.25f};
    // Raise the dim on a picture too bright (or, in the light theme, too
    // dark) for the text, never lowering `bg_dim`.
    bool bg_auto_dim{true};

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

    // 0 = Player, 1 = Live, 3 = Recorder, 4 = ADB, 5 = Settings (App::Tab's
    // numbers); Live by default. 2 was the Playlist tab and opens the Player
    // in Playlist mode.
    int tab{1};
    // 0 = Script, 1 = Playlist (App::PlayerMode).
    int player_mode{};
    // A script reference (see script_reference()/resolve_script_reference()
    // in playlist.hpp), or empty to fall back to the first script found.
    std::string last_script;
    float speed{1.0f};
    int loop_limit{}; // 0 = repeats until stopped
    bool log_open{true};

    // The Live tab's forwarding toggles.
    bool live_touch{true};
    bool live_mouse{};
    bool live_key{true};
    bool live_gamepad{};
    bool live_toggle{};

    // The Live preview's shape and orientation; 0/0 ratio follows the
    // connected touchscreen. See App::live_ratio_w_/h_/live_rotation_.
    int live_ratio_w{};
    int live_ratio_h{};
    int live_rotation{};
    // Whether the phone's screen in the Live preview is glass instead of black.
    bool live_phone_glass{};
    // How clear that glass is, 0 (solid) to 1, and the phone's corner
    // radius in unscaled pixels, 0 to 60, glass or not.
    float live_phone_clear{0.75f};
    int live_phone_rounding{};

    // The Live tab's optional reference image overlay; see
    // LiveImageOverlay::State. Empty path means none was loaded.
    std::string live_image_path;
    float live_image_x{0.5f}; // centre, as a fraction of the preview
    float live_image_y{0.5f};
    float live_image_half_width{0.35f}; // fraction of the preview's width
    float live_image_rotation{0.0f};    // radians
    float live_image_opacity{1.0f};
    bool live_image_locked{};

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

// Look presets: themes/<name>.theme next to the executable, holding only the
// settings about how the window looks (theme, accent, glass, background,
// phone screen, layout).
std::filesystem::path theme_directory();
// Names of the saved presets, sorted.
std::vector<std::string> list_themes();
// Why `name` cannot be a preset's name, or empty if it can.
[[nodiscard]] std::string theme_name_problem(const std::string& name);
bool save_theme(const std::string& name, const Settings& settings, std::string& error);
// Overwrites only the settings the preset holds, over `settings`.
bool load_theme(const std::string& name, Settings& settings, std::string& error);
bool delete_theme(const std::string& name, std::string& error);

} // namespace gui
