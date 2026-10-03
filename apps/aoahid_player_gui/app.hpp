// SPDX-License-Identifier: MIT
#pragma once

#include "activity_log.hpp"
#include "control_api.hpp"
#include "engine.hpp"
#include "live_image.hpp"
#include "playlist.hpp"
#include "settings.hpp"
#include "widgets.hpp"

#include "aoahid_player/adb.hpp"
#include "aoahid_player/event_script.hpp"
#include "aoahid_player/player.hpp"
#include "aoahid_player/recorder.hpp"
#include "aoahid_player/spec_builder.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

// The GUI's frame-rate cap, 0 for none; owned by main(), set from the Live tab.
extern std::atomic<int> g_fps_limit;

namespace gui {

// The whole window: device and profile setup on the left, the player and the
// recorder on the right, the activity log at the bottom. Runs on the UI
// thread; everything slow happens on the Engine, the recording thread, or a
// short-lived adb task.
//
// One class, split across several .cpp files by what each piece draws or
// handles — app.cpp keeps only the lifecycle, settings, the frame loop, and
// the raw input callbacks:
//   app_shell.cpp     — header, nav rail, sidebar, the activity log panel
//   app_devices.cpp   — Devices/Profiles cards and their per-profile settings
//   app_player.cpp    — the Player tab: script picker, transport, keys
//   app_live.cpp      — the Live tab
//   app_playlist.cpp  — the Player tab's Playlist mode
//   app_recorder.cpp  — the Recorder tab
//   app_settings.cpp  — the Settings tab: theme, glass, background, Live look
class App {
  public:
    explicit App(std::function<void()> wake);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void frame();
    // The non-drawing part of frame(): control API requests, finished jobs,
    // recording and bridge results. The main loop calls it alone while the
    // window is minimized and nothing is drawn.
    void background() { poll(); }
    void on_drop(std::vector<std::string> paths);
    void on_focus();
    // The window lost input focus; release a captured pointer so it does not
    // stay hidden and grabbed while some other window is in front.
    void on_focus_lost();
    // Ctrl+F, taken from the window system with the modifier state of that
    // exact key press, so even a chord sent in one burst is recognised.
    void on_search_shortcut();
    // Ctrl+Shift+V on the Live tab: types the clipboard's text on the phone
    // as keystrokes. Same effect as the "Paste Text" button.
    void on_paste_shortcut();
    // Raw key presses from the window system, so live control can forward
    // keys ImGui reserves for itself (Tab, Escape, Space). `scancode` is the
    // platform scancode GLFW hands the key callback, used to recognise JIS
    // (Japanese) keyboard keys GLFW has no key constant for.
    void on_key(int glfw_key, int scancode, bool pressed);
    // Raw cursor position from the window system, tracked independently of
    // ImGui's own io.MousePos so mouse mode's relative motion survives
    // io.MousePos being pinned off-screen (see live_pointer()).
    void on_cursor(double x, double y);

    // True while something on screen moves on its own (progress, spinners,
    // a recording clock), so the main loop keeps redrawing without input.
    [[nodiscard]] bool animating() const;
    // True while the Live tab's full screen mode should be an actual
    // OS-level exclusive full screen window, not just this layout's own
    // preview-filling mode. The window owner (main()) polls this once per
    // loop iteration and drives glfwSetWindowMonitor() accordingly.
    [[nodiscard]] bool wants_os_fullscreen() const noexcept { return live_fullscreen_; }
    // The window owner (main()) reports its windowed geometry here once a
    // frame (skipped while OS-fullscreen), purely so it round-trips through
    // the same persist_settings() as everything else; App does not use it.
    void set_window_geometry(int x, int y, int width, int height) noexcept;
    // True on the one frame after the dark/light toggle changed the theme,
    // and only then: the window owner (main()) polls this once per loop
    // iteration to know when to call theme::apply_style() again, since only
    // it knows the current DPI scale to pass it.
    [[nodiscard]] bool consume_theme_change() noexcept;
    // The UI scale chosen on the Settings tab (one of ui_scales), which the
    // window owner multiplies into the display's DPI scale.
    [[nodiscard]] float ui_scale() const noexcept { return ui_scale_; }

  private:
    // The numbers are what settings.hpp saves; 2 was the Playlist tab, which is
    // now the Player tab's Playlist mode.
    enum class Tab : int { player = 0, live = 1, recorder = 3, adb = 4, settings = 5 };
    // What the Player tab plays: the one loaded script, or a playlist.
    enum class PlayerMode : int { script = 0, playlist = 1 };
    // The Player tab actions whose key can be remapped; the value indexes
    // player_keys_ and player_key_default().
    enum class PlayerAction : int { play = 0, stop = 1, restart = 2, none = 3 };

    // adb server state, as last observed by an adb call the app made; not
    // polled on its own. `unknown` covers "never checked yet" and "the last
    // check could not tell".
    enum class AdbStatus : uint8_t { unknown, not_found, running, stopped };

    // What the Live tab forwards to the phone. Touch and mouse both use the
    // pointer, so only one of them can be on at a time.
    struct LiveToggles {
        bool touch{true};
        bool mouse{};
        bool key{true};
        bool gamepad{};
        bool toggle{};
    };
    // One line of the Live tab's input log.
    struct LiveLogEntry {
        std::string text;
        ImU32 color;
        int64_t time_ns;
    };
    // One active touch contact, whoever is driving it — a script row during
    // playback or the Live tab's own reserved finger (see live_finger()) —
    // for the "active touches" list shown on both the Live and Player tabs.
    struct TouchContact {
        int finger_id;
        int32_t x, y;
    };

    struct AdbList {
        bool ok{};
        std::vector<aoap::AdbDevice> devices;
        std::string error;
    };
    // The one-time adb pre-flight run at startup: start the server so `wm
    // size` answers, read the screen size, then stop it again and give the
    // OS a moment before the initial device scan.
    struct StartupPrep {
        AdbStatus status{AdbStatus::unknown};
        bool size_ok{};
        int32_t width{};
        int32_t height{};
        std::string size_error;
    };
    // The Devices card's refresh button: adb kill-server, then a rescan.
    // `skipped` means a recording was in progress, so adb was left untouched.
    struct RetryPrep {
        bool skipped{};
        AdbStatus status{AdbStatus::unknown};
    };
    // One `adb connect` / `adb disconnect` for an ADB Bridge, or an
    // `adb kill-server` when `restart` is set, off the UI thread. `version` is
    // the connection it belongs to (setup_version()).
    struct AdbJob {
        uint64_t version{};
        size_t device{};
        std::string serial; // "127.0.0.1:<port>"
        bool connect{};
        bool ok{};
        std::string error;
        bool restart{};
    };
    // The ADB tab's state for one connected device (snapshot() index).
    // Whether its bridge runs is DeviceStatus::adb_port, not stored here.
    struct BridgeRow {
        int port{};
        bool busy{};          // a request is on the worker thread
        std::string linked;   // serial `adb connect` reached, else empty
        std::string note;     // last result, shown under the row
        bool note_error{};
    };
    // One file of the csv folder, with what the picker shows and searches.
    struct ScriptFile {
        std::string path;
        std::string name;  // display name
        std::string lower; // name folded for case-insensitive search
        std::string meta;  // "1.2 KB  ·  2026-09-11 06:20"
    };

    // The connection phase, summarised as one label and dot colour; shared
    // by the header's pill and the collapsed sidebar's compact readout.
    struct ConnectionStatus {
        std::string text;
        ImU32 dot;
        bool pulse;
    };
    [[nodiscard]] ConnectionStatus connection_status() const;

    // Layout pieces.
    void draw_header();
    void draw_sidebar();
    void draw_sidebar_collapsed();
    void draw_sidebar_splitter(float height);
    // The Live/Player/Recorder/ADB/Settings switch, as a left icon rail (not a
    // top segmented control), so it reads as part of the window's chrome
    // rather than a tab bar competing with each screen's own content.
    void draw_nav_rail();
    void draw_devices_card();
    void draw_profiles_card();
    void draw_touch_settings();
    void draw_mouse_settings();
    void draw_key_settings();
    void draw_gamepad_settings();
    void draw_pen_settings();
    void draw_toggle_settings();
    void draw_connect_card();
    // The local HTTP control API's on/off toggle and port; see
    // control_api.hpp.
    void draw_control_api_card();
    void draw_player();
    void draw_script_card();
    void draw_script_picker(float width);
    void draw_transport_card();
    void draw_live();
    void draw_live_surface(ImVec2 size);
    void draw_live_log(ImVec2 size);
    // The "active touches" list: every contact currently down, Live's own
    // included — shown on the Live tab's log panel and on the Player tab
    // while a script plays, so multi-finger playback and Live's own touch
    // (which now runs alongside it) are both visible in detail.
    void draw_touch_contacts();
    // The right-hand panel: what is forwarded, the folded settings, and the
    // input log.
    void draw_live_controls(ImVec2 size);
    // The phone-style side keys (volume, brightness), drawn right of the
    // preview, and the media keys and status line below it.
    void draw_live_side_keys(ImVec2 origin, float width, float phone_top, float phone_height);
    void draw_live_media_bar(float width);
    void draw_live_status(float width);
    // Whether a Toggle key press would reach the phone right now.
    [[nodiscard]] bool live_toggle_usable() const;
    void live_tap_key(uint16_t usage);
    // Volume and brightness stay down while their button is held, as on the
    // phone; the frame loop lets go of the key (see release_held_live_key()).
    void live_hold_key(uint16_t usage);
    void release_held_live_key();
    // The "Mouse release key" row: shows the configured key, a button to
    // pick a new one, and a note that only that exact key releases the
    // captured pointer once changed.
    void draw_live_release_key_setting();
    // One "Set..." row for a Player tab action's key.
    void draw_player_key_setting(PlayerAction action, const char* title);
    // The key an action currently answers to: the configured one, else its
    // default (Space, Escape, Home).
    [[nodiscard]] int player_key(PlayerAction action) const noexcept;
    // Why `key` cannot be assigned to `action` (already used by another
    // action, or by the Live tab), or empty if it can.
    [[nodiscard]] std::string player_key_conflict(PlayerAction action, int key) const;
    // The Live preview filling the window, with only the input switches and
    // a way out.
    void draw_live_fullscreen();
    // The switches and the way out, shared by the side panel and the
    // right-click menu of the full screen view.
    void draw_live_fullscreen_switches(float width);
    void draw_live_toggles();
    // The Script | Playlist switch at the top of the Player tab.
    void draw_player_mode_switch();
    // The Playlist mode's cards: the file (name, Open, Save) and the scripts
    // with their loops and the time limit. Playing is the transport card's job.
    void draw_playlist_file_card();
    void draw_playlist_scripts_card();
    void draw_playlist_picker(float width);
    void draw_recorder();
    void draw_record_coords(bool locked);
    void draw_settings();
    void draw_presets_card();
    void draw_appearance_card();
    void draw_glass_card();
    void draw_background_card();
    void draw_live_look_card();
    void draw_layout_card();
    void refresh_themes();
    // Applies every look setting (the part a preset holds) from `settings`.
    void apply_look(const Settings& settings);
    // A short notice in the bottom-right corner (see draw_toasts()), when
    // notifications are on.
    void push_toast(aoap::Severity severity, std::string text);
    // Turns new warnings and errors in the activity log into notices.
    void collect_toasts();
    void draw_toasts();
    // Loads `path` as the window's background picture and switches to it;
    // logs why not when it cannot.
    void load_background(const std::string& path);
    // The background's colour this frame: the theme's, or the chosen one.
    [[nodiscard]] ImU32 background_color() const;
    void draw_log(float height);

    // Actions.
    void poll();
    void refresh_scripts();
    void load_script(const std::string& path);
    void delete_script(const std::string& path);
    // The Connect button: the AOA handshake on the current device selection.
    // Touches neither adb nor the device list; both are kept current by the
    // startup pre-flight and the Devices card's refresh button.
    void connect();
    // The ADB tab: each connected device's ADB Bridge, turned on and off one
    // by one (see Engine::adb_bridge()).
    void draw_adb();
    // Applies Engine bridge results and finished adb jobs; every frame.
    void update_adb_bridges();
    void run_adb_job(size_t device, const std::string& serial, bool connect);
    // Turns the bridge of connected device `index` on or off, as its switch
    // on the ADB tab does; an explanation when that is not possible now.
    // `port` (nonzero) replaces the row's port when the bridge is turned on.
    [[nodiscard]] std::string request_bridge(size_t index, bool wanted, int port = 0);
    void restart_adb_server();
    void accessory();
    // The Devices card's refresh button: adb kill-server, then a rescan. No
    // adb start-server, no screen size, and no handshake — just a cheap way
    // to make USB devices visible again after something else was holding
    // them.
    void retry();
    void toggle_playback();
    void stop_playback();
    void play_playlist();
    void live_enable(bool on);
    void live_pointer(ImVec2 surface_min, ImVec2 surface_size, bool hovered);
    // Grabs or releases the OS pointer for relative mouse forwarding.
    void live_capture_pointer(bool captured);
    // Preview fractions to device fractions and back, following the rotation.
    [[nodiscard]] ImVec2 live_to_device(ImVec2 preview) const noexcept;
    [[nodiscard]] ImVec2 live_to_preview(ImVec2 device) const noexcept;
    // The touch contact slot the Live tab's pointer uses: always the last
    // declared contact, so it never collides with the lower-numbered slots a
    // script uses, and Live can keep touching while a script plays.
    [[nodiscard]] int live_finger() const noexcept;
    // The shape the preview is drawn at, after the rotation.
    [[nodiscard]] float live_preview_aspect() const noexcept;
    void live_keyboard();
    void live_gamepad();
    // Reads the clipboard and types it on the phone; no-op if a paste is
    // already running, the clipboard has no text, or Keyboard live control
    // is not on.
    void live_paste_clipboard();
    void drain_observed();
    void live_log(std::string text, ImU32 color);
    [[nodiscard]] bool live_ready() const;
    void refresh_playlists();
    void load_playlist_named(const std::string& name);
    void save_current_playlist();
    [[nodiscard]] bool playlist_playable() const;
    void seek(aoap::PlaybackPosition target);
    void refresh_adb_devices();
    // Updates the adb status indicator and logs the change, if any.
    void set_adb_status(AdbStatus status);
    void start_recording();
    void stop_recording();
    void finish_recording();
    [[nodiscard]] std::string adb_serial() const;
    [[nodiscard]] aoap::ProfileSetup build_setup() const;
    [[nodiscard]] Settings current_settings() const;
    void apply_settings(const Settings& settings);
    // Writes the settings when they differ from what was last saved.
    void persist_settings();
    [[nodiscard]] bool settings_locked() const;
    [[nodiscard]] bool recording() const;
    // The control API's UiRequests: queued from its thread, carried out by
    // handle_ui_requests() at the start of the next frame.
    UiReply post_ui_request(const UiRequest& request);
    void handle_ui_requests();
    UiReply run_ui_request(const UiRequest& request);

    std::function<void()> wake_;
    ActivityLog log_;
    Engine engine_;
    struct PendingUi {
        uint64_t id{};
        UiRequest request;
        std::promise<UiReply> reply;
    };
    std::mutex ui_requests_mutex_;
    std::deque<PendingUi> ui_requests_;
    bool ui_closing_{};       // guarded by ui_requests_mutex_
    uint64_t ui_request_id_{}; // guarded by ui_requests_mutex_
    UiStatus ui_status_;
    ControlApi control_api_{
        engine_, [this](const UiRequest& request) { return post_ui_request(request); },
        ui_status_};
    // Persisted intent: whether the control API should be listening.
    // control_api_.running() is the actual live state; this is only read
    // again at the next startup (see the constructor) and written back
    // whenever draw_control_api_card() changes it.
    bool api_enabled_{};
    int api_port_{47821};
    bool control_api_open_{}; // the Control API card's folded settings
    // ADB Bridge port per device key (DeviceEntry::key), saved between runs.
    std::map<std::string, int> adb_ports_;
    std::vector<BridgeRow> bridge_rows_;
    uint64_t bridge_version_{~uint64_t{0}}; // setup_version() of bridge_rows_
    std::vector<std::future<AdbJob>> adb_jobs_;
    // Selected in the Recorder once the next adb device list contains it.
    std::string prefer_adb_serial_;
    Tab tab_{Tab::live};
    PlayerMode player_mode_{PlayerMode::script};

    // Devices.
    std::vector<aoap::DeviceEntry> devices_;
    uint64_t devices_version_{~uint64_t{0}};
    std::set<std::string> selected_;
    std::string connect_error_;
    float connect_height_{120.0f};
    float sidebar_width_{392.0f};
    // Collapses Devices/Profiles/Connect to a slim rail so the Player/Live/
    // Playlist/Recorder pane can use the full width; meant for once a device
    // is already connected and the setup cards are not needed for a while.
    bool sidebar_collapsed_{};
    bool dark_theme_{true};
    // Set by the Settings tab's theme switch, cleared by consume_theme_change().
    bool theme_dirty_{};
    // The Settings tab; see Settings for what each means.
    int accent_{0xB4A5FF};
    float glass_{0.4f};
    float gloss_{1.0f};
    float rim_{1.0f};
    int card_rounding_{12};
    bool motion_{true};
    float ui_scale_{1.0f};
    bool sidebar_right_{};
    bool log_hidden_{};
    bool toasts_enabled_{true};
    // The tab drawn last frame, and when the tab last changed, for the
    // short fade-in of the new one.
    static constexpr double tab_fade_seconds = 0.15;
    Tab shown_tab_{Tab::live};
    double tab_changed_at_{-1.0};
    // Notices on screen, oldest first; see push_toast().
    struct Toast {
        uint64_t id;
        aoap::Severity severity;
        std::string text;
        double shown_at;
        double hide_at;
        bool dismissed; // clicked away: fading out, hover no longer holds it
    };
    std::deque<Toast> toasts_;
    uint64_t toast_next_id_{};
    uint64_t toast_log_seen_{}; // log_.version() already turned into notices
    // Look presets (themes/<name>.theme).
    std::vector<std::string> theme_names_;
    std::string theme_selected_;
    std::string theme_name_input_;
    std::string theme_pending_delete_;
    std::string theme_current_; // the last one applied or saved, shown only
    bool themes_stale_{true};   // re-read the list the next time it is drawn
    int bg_mode_{}; // 0 theme colour, 1 bg_color_, 2 picture (see backdrop.hpp)
    int bg_color_{0x14131A};
    int bg_blur_{6};
    float bg_dim_{0.4f};
    // The picture to show, kept even when it could not be loaded (a drive
    // not mounted yet), so the next start tries it again.
    std::string bg_image_;
    std::string bg_image_input_;
    // Last windowed geometry main() reported (see set_window_geometry());
    // 0 width/height means "never reported yet".
    int window_x_{};
    int window_y_{};
    int window_width_{};
    int window_height_{};
    Engine::Phase last_phase_{Engine::Phase::idle};
    AdbStatus adb_status_{AdbStatus::unknown};

    // Profile settings; frozen while connected.
    bool use_touch_{};
    bool use_mouse_{};
    bool use_key_{};
    bool use_gamepad_{};
    bool use_pen_{};
    bool use_toggle_{};
    // Which profile rows have their settings unfolded (same order as the card).
    bool profile_open_[6]{};
    // Where the touchscreen size came from: the startup adb read, the user's
    // own typing, or neither (the saved or built-in value, which may not
    // match the phone).
    enum class TouchSize : uint8_t { pending, from_phone, not_read, typed };
    TouchSize touch_size_{TouchSize::pending};
    int touch_width_{1440};
    int touch_height_{2560};
    // 16 by default (the maximum) so the Live tab's reserved top slot (see
    // live_finger()) is always available alongside whatever a script uses.
    int touch_contacts_{16};
    int mouse_buttons_{5};
    int key_min_{0x04};
    int key_max_{0x65};
    int pad_buttons_{16};
    int pad_bits_{16};
    int pad_dpad_{1}; // 0 none, 1 hat, 2 buttons
    std::vector<aoahid_axis_role> pad_axes_;
    int pen_mode_{0}; // 0 direct screen, 1 indirect tablet

    // Scripts.
    std::vector<ScriptFile> scripts_;
    std::string script_path_;
    // script_reference(script_path_), kept so saving settings each frame does
    // not resolve paths on disk.
    std::string script_reference_;
    std::shared_ptr<const aoap::EventScript> script_;
    aoap::Timeline timeline_;
    // The scripts of the playlist being played, each with its timeline, so the
    // transport can draw the step that is running (see play_playlist()).
    struct RunStep {
        std::shared_ptr<const aoap::EventScript> script;
        aoap::Timeline timeline;
    };
    std::vector<RunStep> run_steps_;
    std::vector<std::string> script_errors_;   // every problem of the last failed load
    std::vector<std::string> script_warnings_; // aoap::lap_warnings() of script_
    std::string path_input_;
    std::string script_filter_;
    bool focus_search_{};
    bool open_picker_{};
    int picker_cursor_{};
    bool picker_follow_{}; // scroll the list to the cursor row
    bool picker_typing_{}; // the search field was active last frame
    std::string pending_delete_;

    // Transport.
    aoap::PlaybackPosition cursor_{};
    float scrub_timeline_{};
    float speed_{1.0f};
    int loop_limit_{};
    float offset_step_ms_{10.0f};

    // Live tab.
    LiveToggles live_{};
    bool live_capturing_{};   // the pointer belongs to the surface
    // While in mouse mode, the OS cursor is grabbed (hidden and unbounded) so
    // relative motion keeps flowing even past the edge of the screen. The
    // release key (Escape by default; see live_release_key_) releases it
    // without turning Live control or the Mouse toggle off; clicking the
    // preview again grabs it back.
    bool live_mouse_captured_{};
    // The click that grabbed the pointer is still held; its left button is
    // not forwarded, so grabbing never clicks on the phone.
    bool live_swallow_left_{};
    // The GLFW key that releases the captured pointer; 0 means "not set",
    // which is treated as Escape. Only this exact key releases the capture
    // once changed — every other key (including the mouse's own buttons)
    // still goes straight to the phone, capture or not.
    int live_release_key_{};
    // True while "Set release key" is armed: the next key press this frame
    // is captured as the new live_release_key_ instead of being forwarded.
    bool live_release_key_picking_{};
    // Configured GLFW keys for the Player tab's play/pause, stop, and
    // back-to-start actions; 0 means "not set", which keeps the default
    // key(s) (see player_key()).
    int player_keys_[3]{};
    bool player_keys_open_{}; // the transport card's folded key settings
    // The action whose "Set..." is armed; the next key press becomes its key.
    PlayerAction player_key_picking_{PlayerAction::none};
    // Shown while picking when the last press could not be assigned.
    std::string player_key_conflict_;
    bool player_key_was_picking_{}; // player_key_picking_ was armed last frame
    // The last key press seen by on_key(), consumed by frame() to run a
    // remapped action (ImGui's own key state has no GLFW key codes).
    int pending_player_key_{};
    bool live_touching_{};    // the Live tab's own contact is down
    int32_t live_touch_x_{};  // its last position, in device coordinates
    int32_t live_touch_y_{};
    // Every touch contact currently down, Live's own included, keyed by
    // finger_id; fed by drain_observed() (see Engine::set_observing()) and
    // shown on both the Live and Player tabs (see draw_touch_contacts()).
    std::vector<TouchContact> touch_contacts_active_;
    // The preview's shape, as a width:height ratio; 0 follows the connected
    // touchscreen. `live_rotation_` is how many quarter turns clockwise the
    // phone is shown at, for landscape use.
    int live_ratio_w_{};
    int live_ratio_h_{};
    int live_rotation_{};
    bool live_phone_glass_{}; // the phone's screen is glass instead of black
    float live_phone_clear_{0.4f};
    int live_phone_rounding_{}; // unscaled pixels
    bool live_fullscreen_{};
    uint16_t live_held_key_{}; // the side key held down now, 0 for none
    // Which folded rows of the Live panel are open.
    bool live_fold_release_{};
    bool live_fold_shape_{};
    // The phone rectangle as last drawn, so full screen knows where its side
    // margins are.
    ImVec2 live_phone_min_{};
    ImVec2 live_phone_max_{};
    std::vector<LiveLogEntry> live_log_lines_;
    uint64_t live_log_version_{};
    uint64_t live_log_seen_{};
    std::vector<uint16_t> live_keys_;      // held keys, for the on-screen list
    std::vector<uint32_t> live_buttons_;   // held mouse buttons
    std::vector<uint32_t> live_pad_;       // held gamepad buttons
    int64_t live_wheel_at_{};
    int32_t live_wheel_{};
    // The last relative motion the phone actually received, for the panel.
    int32_t live_move_sent_x_{};
    int32_t live_move_sent_y_{};
    int64_t live_move_at_{};
    bool live_pad_connected_{};
    aoap::GamepadDpad live_dpad_{};
    std::vector<int32_t> live_axes_;
    std::vector<uint16_t> pressed_keys_;  // HID usages seen since the last frame; key-ups go
                                           // straight to the engine instead (see on_key())
    double live_move_x_{};    // pointer motion not yet sent as whole pixels
    double live_move_y_{};
    // Raw cursor position from the window system, used to compute motion
    // independently of ImGui's io.MouseDelta: while the pointer is captured,
    // io.MousePos is pinned off-screen every frame (see frame()) so a
    // captured, invisible, unboundedly-moving cursor can never drift onto —
    // and click — some other button in this window. ImGui's own delta
    // calculation depends on io.MousePos actually moving, so once it is
    // pinned this is the only source of the pointer's motion. on_cursor()
    // sends each raw motion event straight to the engine as it arrives,
    // rather than waiting for the next rendered frame to drain it, so
    // dragging is not held up by the render/vsync cadence.
    bool live_raw_cursor_valid_{};
    double live_raw_cursor_x_{};
    double live_raw_cursor_y_{};
    LiveImageOverlay live_image_;      // optional reference picture over the preview
    std::string live_image_path_input_;
    // Clipboard paste: types characters on a worker thread so the ~8ms
    // per-character pacing (see live_paste_clipboard()) never blocks the UI.
    std::thread live_paste_thread_;
    std::atomic<bool> live_paste_active_{};

    // Playlist.
    // A saved playlist as the picker lists it.
    struct PlaylistFile {
        std::string name;
        std::string lower; // for searching
        std::string meta;  // "3 scripts"
    };
    std::vector<PlaylistFile> playlist_files_;
    // The playlist picker: the same search, cursor, and delete confirmation
    // as the script picker's.
    std::string playlist_filter_;
    std::string playlist_pending_delete_;
    int playlist_cursor_{};
    bool playlist_follow_{};
    bool playlist_focus_search_{};
    bool playlist_typing_{};
    Playlist playlist_;
    std::string playlist_name_input_;
    std::string playlist_error_;
    bool playlist_dirty_{};
    int playlist_add_choice_{};
    // The running step the list last scrolled to, so it follows the run once
    // per step and leaves the user's own scrolling alone in between.
    size_t playlist_shown_step_{SIZE_MAX};

    // Recorder.
    std::vector<aoap::AdbDevice> adb_devices_;
    int adb_choice_{}; // 0 = automatic, else adb_devices_[choice - 1]
    std::string record_name_;
    std::string record_input_;
    int record_coords_{0}; // aoap::CoordMode's order: raw, normalized
    std::unique_ptr<aoap::Recorder> recorder_;
    std::thread record_thread_;
    std::atomic<bool> record_done_{};
    bool record_saved_{};
    std::string record_path_;
    std::string last_record_path_;
    uint64_t last_record_rows_{};

    // Short adb tasks.
    std::future<StartupPrep> startup_prep_task_;
    std::future<RetryPrep> retry_task_;
    std::future<AdbList> adb_task_;

    // Saved between runs.
    std::filesystem::path settings_path_;
    Settings saved_settings_;
    bool settings_warned_{};

    // Log panel.
    bool log_open_{true};
    uint64_t log_seen_{};
    bool log_follow_{true};
};

} // namespace gui
