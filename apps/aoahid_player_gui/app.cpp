// SPDX-License-Identifier: MIT
#include "app.hpp"

#include "app_common.hpp"
#include "keymap.hpp"
#include "theme.hpp"

#include "aoahid_player/device.hpp"
#include "aoahid_player/paths.hpp"
#include "aoahid_player/timing.hpp"

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string_view>
#include <utility>

namespace gui {
using namespace detail;

App::App(std::function<void()> wake)
    : wake_(std::move(wake)), log_(wake_), engine_(log_, wake_) {
    settings_path_ = settings_path();
    const Settings loaded = load_settings(settings_path_);
    apply_settings(loaded);
    saved_settings_ = current_settings();
    engine_.player().set_speed(speed_);
    engine_.player().set_loop_limit(loop_limit_);
    if (api_enabled_) {
        const std::string error = control_api_.start(api_port_);
        if (!error.empty()) {
            log_.message(aoap::Severity::error, error);
            api_enabled_ = false;
        }
    }
    refresh_scripts();
    refresh_playlists();
    // Picks up where the last run left off when that script is still there;
    // otherwise, as before, whatever sorts first.
    std::string startup_script;
    if (!loaded.last_script.empty()) {
        const std::string resolved = resolve_script_reference(loaded.last_script);
        for (const ScriptFile& file : scripts_) {
            if (file.path == resolved) {
                startup_script = resolved;
                break;
            }
        }
    }
    if (startup_script.empty() && !scripts_.empty())
        startup_script = scripts_.front().path;
    if (!startup_script.empty())
        load_script(startup_script);
    log_.message(aoap::Severity::info,
                 "Scripts folder: " + aoap::script_directory() + ". Searching for devices...");

    // One-time adb pre-flight: read the screen size while the server is up,
    // then stop it so it does not hold the phone's USB interface during the
    // scan below, or later during Connect's own AOA handshake.
    startup_prep_task_ =
        std::async(std::launch::async, [serial = adb_serial(), wake = wake_] {
            StartupPrep result;
            std::string error;
            if (aoap::adb_start_server(error))
                result.status = AdbStatus::running;
            else
                result.status =
                    aoap::adb_missing(error) ? AdbStatus::not_found : AdbStatus::unknown;
            result.size_ok =
                aoap::adb_screen_size(serial, result.width, result.height, result.size_error);
            std::string stop_error;
            if (aoap::adb_kill_server(stop_error)) {
                if (result.status != AdbStatus::not_found)
                    result.status = AdbStatus::stopped;
            } else if (aoap::adb_missing(stop_error)) {
                result.status = AdbStatus::not_found;
            }
            aoap::Timing::sleep_ms(1000);
            wake();
            return result;
        });
}

App::~App() {
    live_capture_pointer(false);
    persist_settings();
    stop_recording();
    if (live_paste_thread_.joinable())
        live_paste_thread_.join();
    if (record_thread_.joinable())
        record_thread_.join();
}

bool App::recording() const { return record_thread_.joinable(); }

bool App::settings_locked() const { return engine_.phase() != Phase::idle; }

bool App::animating() const {
    if (engine_.busy() || recording() || startup_prep_task_.valid() || retry_task_.valid() ||
        adb_task_.valid() || ui::animations_active())
        return true;
    if (engine_.phase() == Phase::playing &&
        (engine_.player().status().state != aoap::PlaybackState::paused ||
         engine_.playlist_progress().ends_at_ns != 0))
        return true;
    // Live control polls the gamepad and shows what the phone is doing.
    return engine_.live_active();
}

std::string App::adb_serial() const {
    if (adb_choice_ > 0 && static_cast<size_t>(adb_choice_) <= adb_devices_.size())
        return adb_devices_[static_cast<size_t>(adb_choice_) - 1].serial;
    return {};
}

Settings App::current_settings() const {
    Settings settings;
    settings.use_touch = use_touch_;
    settings.touch_width = touch_width_;
    settings.touch_height = touch_height_;
    settings.touch_contacts = touch_contacts_;
    settings.use_mouse = use_mouse_;
    settings.mouse_buttons = mouse_buttons_;
    settings.use_key = use_key_;
    settings.key_min = key_min_;
    settings.key_max = key_max_;
    settings.use_gamepad = use_gamepad_;
    settings.pad_buttons = pad_buttons_;
    settings.pad_bits = pad_bits_;
    settings.pad_dpad = pad_dpad_;
    settings.pad_axes = pad_axes_;
    settings.use_pen = use_pen_;
    settings.pen_mode = pen_mode_;
    settings.api_enabled = api_enabled_;
    settings.api_port = api_port_;
    settings.record_coords = record_coords_;
    settings.live_release_key = live_release_key_;
    settings.player_play_key = player_keys_[0];
    settings.player_stop_key = player_keys_[1];
    settings.player_restart_key = player_keys_[2];
    settings.sidebar_width = sidebar_width_;
    settings.sidebar_collapsed = sidebar_collapsed_;
    settings.dark_theme = dark_theme_;
    settings.window_x = window_x_;
    settings.window_y = window_y_;
    settings.window_width = window_width_;
    settings.window_height = window_height_;
    settings.tab = static_cast<int>(tab_);
    settings.last_script = script_path_.empty() ? std::string() : script_reference(script_path_);
    settings.speed = speed_;
    settings.loop_limit = loop_limit_;
    settings.log_open = log_open_;
    settings.live_touch = live_.touch;
    settings.live_mouse = live_.mouse;
    settings.live_key = live_.key;
    settings.live_gamepad = live_.gamepad;
    settings.live_ratio_w = live_ratio_w_;
    settings.live_ratio_h = live_ratio_h_;
    settings.live_rotation = live_rotation_;
    const LiveImageOverlay::State image = live_image_.snapshot();
    settings.live_image_path = image.path;
    settings.live_image_x = image.center.x;
    settings.live_image_y = image.center.y;
    settings.live_image_half_width = image.half_width_frac;
    settings.live_image_rotation = image.rotation;
    settings.live_image_opacity = image.alpha;
    return settings;
}

void App::apply_settings(const Settings& settings) {
    use_touch_ = settings.use_touch;
    touch_width_ = settings.touch_width;
    touch_height_ = settings.touch_height;
    touch_contacts_ = settings.touch_contacts;
    use_mouse_ = settings.use_mouse;
    mouse_buttons_ = settings.mouse_buttons;
    use_key_ = settings.use_key;
    key_min_ = settings.key_min;
    key_max_ = settings.key_max;
    use_gamepad_ = settings.use_gamepad;
    pad_buttons_ = settings.pad_buttons;
    pad_bits_ = settings.pad_bits;
    pad_dpad_ = settings.pad_dpad;
    pad_axes_ = settings.pad_axes;
    use_pen_ = settings.use_pen;
    pen_mode_ = settings.pen_mode;
    api_enabled_ = settings.api_enabled;
    api_port_ = settings.api_port;
    record_coords_ = std::clamp(settings.record_coords, 0, 1);
    live_release_key_ = settings.live_release_key;
    player_keys_[0] = settings.player_play_key;
    player_keys_[1] = settings.player_stop_key;
    player_keys_[2] = settings.player_restart_key;
    sidebar_width_ = settings.sidebar_width;
    sidebar_collapsed_ = settings.sidebar_collapsed;
    // Not re-applied here: main() already set the theme from this same file
    // before the window (and this App) existed, so the two never disagree.
    dark_theme_ = settings.dark_theme;
    window_x_ = settings.window_x;
    window_y_ = settings.window_y;
    window_width_ = settings.window_width;
    window_height_ = settings.window_height;
    tab_ = settings.tab >= 0 && settings.tab <= 3 ? static_cast<Tab>(settings.tab) : Tab::player;
    speed_ = settings.speed;
    loop_limit_ = settings.loop_limit;
    log_open_ = settings.log_open;
    live_.touch = settings.live_touch;
    live_.mouse = settings.live_mouse;
    live_.key = settings.live_key;
    live_.gamepad = settings.live_gamepad;
    live_ratio_w_ = settings.live_ratio_w;
    live_ratio_h_ = settings.live_ratio_h;
    live_rotation_ = settings.live_rotation & 3;
    if (!settings.live_image_path.empty()) {
        LiveImageOverlay::State image;
        image.path = settings.live_image_path;
        image.center = ImVec2(settings.live_image_x, settings.live_image_y);
        image.half_width_frac = settings.live_image_half_width;
        image.rotation = settings.live_image_rotation;
        image.alpha = settings.live_image_opacity;
        const std::string error = live_image_.restore(image);
        if (!error.empty())
            log_.message(aoap::Severity::warning, "Reference image: " + error);
    }
}

void App::set_window_geometry(const int x, const int y, const int width,
                              const int height) noexcept {
    window_x_ = x;
    window_y_ = y;
    window_width_ = width;
    window_height_ = height;
}

bool App::consume_theme_change() noexcept {
    const bool changed = theme_dirty_;
    theme_dirty_ = false;
    return changed;
}

void App::persist_settings() {
    Settings current = current_settings();
    if (current == saved_settings_)
        return;
    std::string error;
    if (!save_settings(settings_path_, current, error) && !settings_warned_) {
        // Said once; a read-only folder should not flood the log.
        settings_warned_ = true;
        log_.message(aoap::Severity::warning,
                     "Settings cannot be saved (" + error + "); changes last until you close "
                     "the app.");
    }
    saved_settings_ = std::move(current);
}

aoap::ProfileSetup App::build_setup() const {
    aoap::ProfileSetup setup;
    setup.touch = {use_touch_, touch_width_, touch_height_, static_cast<uint32_t>(touch_contacts_)};
    setup.mouse = {use_mouse_, static_cast<uint32_t>(mouse_buttons_)};
    setup.key = {use_key_, static_cast<uint16_t>(key_min_), static_cast<uint16_t>(key_max_)};
    setup.gamepad.enabled = use_gamepad_;
    setup.gamepad.buttons = static_cast<uint32_t>(pad_buttons_);
    setup.gamepad.axis_bits = static_cast<uint32_t>(pad_bits_);
    setup.gamepad.dpad = pad_dpad_ == 0   ? AOAHID_DPAD_NONE
                         : pad_dpad_ == 1 ? AOAHID_DPAD_HAT
                                          : AOAHID_DPAD_BUTTONS;
    setup.gamepad.axes = pad_axes_;
    setup.pen.enabled = use_pen_;
    setup.pen.mode = pen_mode_ == 0 ? AOAHID_PEN_DIRECT_SCREEN : AOAHID_PEN_INDIRECT_TABLET;
    aoap::resolve_pen_surface(setup);
    return setup;
}

// --- Actions ---------------------------------------------------------------

void App::poll() {
    const uint64_t version = engine_.devices_version();
    if (version != devices_version_) {
        devices_version_ = version;
        devices_ = engine_.devices();
        // With exactly one phone there is nothing to choose.
        if (devices_.size() == 1 && selected_.empty())
            selected_.insert(devices_.front().key);
    }

    // The Player tab also observes now, so its active-touches list (see
    // draw_transport_card()) stays current even when the Live tab is not
    // the one on screen.
    engine_.set_observing(tab_ == Tab::live || tab_ == Tab::player);
    const Phase phase = engine_.phase();
    if (phase != last_phase_) {
        if (last_phase_ == Phase::playing)
            cursor_ = {};
        if (phase == Phase::connected && last_phase_ == Phase::connecting)
            connect_error_.clear();
        last_phase_ = phase;
    }

    if (ready(startup_prep_task_)) {
        const StartupPrep result = startup_prep_task_.get();
        set_adb_status(result.status);
        if (result.size_ok) {
            touch_width_ = result.width;
            touch_height_ = result.height;
            log_.message(aoap::Severity::info, "Screen size from adb: " +
                                                   std::to_string(result.width) + " x " +
                                                   std::to_string(result.height) + ".");
        } else {
            log_.message(aoap::Severity::warning,
                         "Screen size not detected. " + result.size_error);
        }
        engine_.refresh();
    }
    if (ready(retry_task_)) {
        const RetryPrep result = retry_task_.get();
        if (!result.skipped)
            set_adb_status(result.status);
        engine_.refresh();
    }
    if (ready(adb_task_)) {
        AdbList result = adb_task_.get();
        const std::string previous = adb_serial();
        adb_devices_ = std::move(result.devices);
        adb_choice_ = 0;
        for (size_t index = 0; index < adb_devices_.size(); ++index) {
            if (adb_devices_[index].serial == previous)
                adb_choice_ = static_cast<int>(index) + 1;
        }
        if (result.ok)
            set_adb_status(AdbStatus::running);
        else {
            log_.message(aoap::Severity::warning, result.error);
            if (aoap::adb_missing(result.error))
                set_adb_status(AdbStatus::not_found);
        }
    }
    if (record_done_.load(std::memory_order_acquire))
        finish_recording();
}

void App::refresh_scripts() {
    scripts_.clear();
    for (std::string& path : aoap::discover_csv_files(aoap::script_directory())) {
        ScriptFile file;
        file.name = aoap::display_name(path);
        file.lower = fold(file.name);
        file.meta = file_meta(path);
        file.path = std::move(path);
        scripts_.push_back(std::move(file));
    }
}

void App::delete_script(const std::string& path) {
    std::error_code code;
    const std::string name = aoap::display_name(path);
    if (!std::filesystem::remove(aoap::utf8_path(path), code) || code) {
        log_.message(aoap::Severity::error, "Could not delete " + name + ".csv" +
                                                (code ? ": " + code.message() : std::string()) +
                                                ".");
        refresh_scripts();
        return;
    }
    log_.message(aoap::Severity::info, "Deleted " + name + ".csv.");
    if (path == script_path_) {
        script_.reset();
        script_path_.clear();
        script_warnings_.clear();
        timeline_ = {};
        cursor_ = {};
    }
    if (path == last_record_path_)
        last_record_path_.clear();
    refresh_scripts();
}

void App::load_script(const std::string& path) {
    auto script = std::make_shared<aoap::EventScript>();
    std::string error;
    if (!script->load(path, error)) {
        script_errors_ = script->errors;
        log_.message(aoap::Severity::error, error);
        return;
    }
    timeline_ = aoap::build_timeline(*script);
    script_warnings_ = aoap::lap_warnings(*script, timeline_);
    for (const std::string& warning : script_warnings_)
        log_.message(aoap::Severity::warning, aoap::display_name(path) + ": " + warning);
    script_ = std::move(script);
    script_path_ = path;
    path_input_ = path;
    script_errors_.clear();
    cursor_ = {};
}

void App::accessory() {
    connect_error_.clear();
    std::vector<size_t> selection;
    for (size_t index = 0; index < devices_.size(); ++index) {
        if (selected_.count(devices_[index].key) != 0)
            selection.push_back(index);
    }
    if (selection.empty()) {
        connect_error_ = devices_.empty() ? "No device found. Plug in a phone and refresh."
                                          : "Select at least one device.";
        return;
    }
    engine_.accessory(std::move(selection));
}

void App::connect() {
    connect_error_.clear();
    std::vector<size_t> selection;
    for (size_t index = 0; index < devices_.size(); ++index) {
        if (selected_.count(devices_[index].key) != 0)
            selection.push_back(index);
    }
    if (selection.empty()) {
        connect_error_ = devices_.empty() ? "No device found. Plug in a phone and refresh."
                                          : "Select at least one device.";
        return;
    }
    const aoap::ProfileSetup setup = build_setup();
    std::string error;
    if (!aoap::validate_setup(setup, error)) {
        connect_error_ = error;
        return;
    }
    engine_.connect(std::move(selection), setup);
}

void App::retry() {
    if (startup_prep_task_.valid() || retry_task_.valid() || engine_.phase() != Phase::idle)
        return;
    connect_error_.clear();
    const bool skip_adb = recording();
    if (skip_adb)
        log_.message(aoap::Severity::info,
                     "The adb server is left running because a recording is in progress.");
    retry_task_ = std::async(std::launch::async, [skip_adb, wake = wake_] {
        RetryPrep result;
        if (!skip_adb) {
            std::string error;
            if (aoap::adb_kill_server(error)) {
                result.status = AdbStatus::stopped;
                aoap::Timing::sleep_ms(800);
            } else {
                result.status =
                    aoap::adb_missing(error) ? AdbStatus::not_found : AdbStatus::unknown;
            }
        } else {
            result.skipped = true;
        }
        wake();
        return result;
    });
}

void App::set_adb_status(const AdbStatus status) {
    if (status == adb_status_)
        return;
    adb_status_ = status;
    switch (status) {
    case AdbStatus::not_found:
        log_.message(aoap::Severity::warning, "adb not found on PATH.");
        break;
    case AdbStatus::running:
        log_.message(aoap::Severity::info, "adb server running.");
        break;
    case AdbStatus::stopped:
        log_.message(aoap::Severity::info, "adb server stopped.");
        break;
    case AdbStatus::unknown:
        break;
    }
}

void App::toggle_playback() {
    const Phase phase = engine_.phase();
    if (phase == Phase::connected) {
        if (script_)
            engine_.play(script_, aoap::display_name(script_path_), cursor_, loop_limit_);
        return;
    }
    if (phase != Phase::playing)
        return;
    const aoap::PlaybackState state = engine_.player().status().state;
    if (state == aoap::PlaybackState::paused)
        engine_.player().resume();
    else if (state == aoap::PlaybackState::playing)
        engine_.player().pause();
}

bool App::playlist_playable() const {
    if (playlist_.entries.empty())
        return false;
    for (const Playlist::Entry& entry : playlist_.entries) {
        const std::string path = resolve_script_reference(entry.script);
        std::error_code code;
        if (!std::filesystem::exists(aoap::utf8_path(path), code))
            return false;
    }
    return true;
}

void App::refresh_playlists() { playlist_names_ = list_playlists(); }

void App::load_playlist_named(const std::string& name) {
    Playlist loaded;
    std::string error;
    if (!load_playlist(name, loaded, error)) {
        playlist_error_ = error;
        log_.message(aoap::Severity::error, error);
        refresh_playlists();
        return;
    }
    playlist_ = std::move(loaded);
    playlist_name_input_ = playlist_.name;
    playlist_dirty_ = false;
    playlist_error_.clear();
}

void App::save_current_playlist() {
    Playlist saving = playlist_;
    saving.name = playlist_name_input_;
    const std::string problem = record_name_problem(saving.name);
    if (saving.name.empty()) {
        playlist_error_ = "Give the playlist a name before saving.";
        return;
    }
    if (!problem.empty()) {
        playlist_error_ = problem;
        return;
    }
    std::string error;
    if (!save_playlist(saving, error)) {
        playlist_error_ = error;
        log_.message(aoap::Severity::error, error);
        return;
    }
    playlist_ = std::move(saving);
    playlist_dirty_ = false;
    playlist_error_.clear();
    refresh_playlists();
    log_.message(aoap::Severity::info, "Saved the playlist \"" + playlist_.name + "\".");
}

void App::play_playlist() {
    std::vector<PlaylistStep> steps;
    steps.reserve(playlist_.entries.size());
    for (const Playlist::Entry& entry : playlist_.entries) {
        const std::string path = resolve_script_reference(entry.script);
        auto script = std::make_shared<aoap::EventScript>();
        std::string error;
        if (!script->load(path, error)) {
            playlist_error_ = error;
            log_.message(aoap::Severity::error, error);
            return;
        }
        steps.push_back(PlaylistStep{std::move(script), aoap::display_name(path), entry.loops});
    }
    playlist_error_.clear();
    const int64_t limit = static_cast<int64_t>(playlist_.time_limit_minutes) * 60 *
                          1'000'000'000LL;
    engine_.play_playlist(std::move(steps), limit);
}

void App::stop_playback() {
    engine_.stop();
    cursor_ = {};
}

void App::seek(const aoap::PlaybackPosition target) {
    if (engine_.phase() == Phase::playing &&
        engine_.player().status().state != aoap::PlaybackState::stopped)
        engine_.player().seek(target);
    else
        cursor_ = target;
}

void App::refresh_adb_devices() {
    if (adb_task_.valid())
        return;
    adb_task_ = std::async(std::launch::async, [wake = wake_] {
        AdbList result;
        result.ok = aoap::adb_list_devices(result.devices, result.error);
        wake();
        return result;
    });
}

void App::start_recording() {
    if (recording())
        return;
    if (!record_name_problem(record_name_).empty())
        return;
    aoap::RecordOptions options;
    options.output_path = aoap::resolve_record_path(
        record_name_.empty() ? std::string() : record_file_name(record_name_));
    options.adb_serial = adb_serial();
    options.input_device = record_input_;
    options.coords = static_cast<aoap::CoordMode>(std::clamp(record_coords_, 0, 1));
    record_path_ = options.output_path;
    record_saved_ = false;
    record_done_.store(false, std::memory_order_relaxed);
    recorder_ = std::make_unique<aoap::Recorder>(&log_);
    record_thread_ = std::thread([this, options] {
        record_saved_ = recorder_->run(options);
        record_done_.store(true, std::memory_order_release);
        wake_();
    });
}

void App::stop_recording() {
    if (recorder_)
        recorder_->stop();
}

void App::finish_recording() {
    if (record_thread_.joinable())
        record_thread_.join();
    record_done_.store(false, std::memory_order_relaxed);
    if (recorder_ && record_saved_) {
        last_record_path_ = record_path_;
        last_record_rows_ = recorder_->rows();
        record_name_.clear();
        refresh_scripts();
    }
    recorder_.reset();
}

void App::on_drop(std::vector<std::string> paths) {
    for (const std::string& path : paths) {
        if (has_csv_extension(path)) {
            if (engine_.phase() == Phase::playing) {
                log_.message(aoap::Severity::warning,
                             "Stop playback before loading another script.");
                return;
            }
            tab_ = Tab::player;
            load_script(path);
            return;
        }
    }
    if (tab_ == Tab::live) {
        for (const std::string& path : paths) {
            if (has_image_extension(path)) {
                const std::string error = live_image_.load(path);
                if (!error.empty())
                    log_.message(aoap::Severity::warning, error);
                return;
            }
        }
        log_.message(aoap::Severity::warning,
                     "Drop a .csv script, or a .png/.jpg/.bmp image for the reference "
                     "overlay, here.");
        return;
    }
    log_.message(aoap::Severity::warning, "Only .csv scripts can be dropped here.");
}

void App::on_focus() {
    refresh_scripts();
    refresh_playlists();
}

void App::on_focus_lost() { live_capture_pointer(false); }

void App::on_key(const int glfw_key, const int scancode, const bool pressed) {
    if (live_release_key_picking_) {
        // Capturing the next press for "Set release key"; nothing else
        // touches this key press. Escape cancels back to the default instead
        // of becoming the release key itself, since that would make Escape
        // unreachable as a way to back out of the picker.
        if (pressed) {
            live_release_key_picking_ = false;
            live_release_key_ = glfw_key == GLFW_KEY_ESCAPE ? 0 : glfw_key;
        }
        return;
    }
    if (player_key_picking_ != PlayerAction::none) {
        if (!pressed)
            return;
        if (glfw_key == GLFW_KEY_ESCAPE) {
            player_key_picking_ = PlayerAction::none;
        } else if (glfw_key != GLFW_KEY_UNKNOWN) {
            player_key_conflict_ = player_key_conflict(player_key_picking_, glfw_key);
            if (player_key_conflict_.empty()) {
                player_keys_[static_cast<int>(player_key_picking_)] = glfw_key;
                player_key_picking_ = PlayerAction::none;
            }
        }
        return;
    }
    if (pressed)
        pending_player_key_ = glfw_key;
    // The release key lets go of the captured pointer no matter which
    // profiles are forwarding, so it works in mouse-only mode too. Like Esc
    // used to, it is still forwarded to the phone afterwards if Keyboard is
    // also on — releasing the capture does not consume the press.
    if (pressed && live_mouse_captured_ && engine_.live_active() &&
        is_release_key(glfw_key, live_release_key_))
        live_capture_pointer(false);

    if (!live_.key || !engine_.live_active())
        return;
    if (is_release_key(glfw_key, live_release_key_) || ImGui::GetIO().WantTextInput)
        return;
    // GLFW has no key constant for the keys a JIS (Japanese) keyboard adds
    // over a US layout — Henkan, Muhenkan, Kana, Zenkaku/Hankaku, Ro — so
    // they decode to GLFW_KEY_UNKNOWN and hid_usage_from_glfw() cannot see
    // them. This is not an Android-side limitation: the key never left this
    // app. The platform scancode still identifies them on Linux.
    const uint16_t usage = hid_usage_from_glfw(glfw_key);
    const uint16_t resolved = usage != 0 ? usage : hid_usage_from_scancode(scancode);
    if (resolved == 0)
        return;
    if (!pressed) {
        // Always deliver key-ups, even off the Live tab (live_keyboard() only
        // drains the queue while that tab is drawn): otherwise a key held
        // while switching tabs stays stuck down on the phone until Live
        // control is turned off entirely.
        engine_.live_send(aoap::KeyEvent{resolved, false});
        return;
    }
    if (tab_ != Tab::live)
        return; // new presses only start while the Live tab is open
    pressed_keys_.push_back(resolved);
}

void App::on_cursor(const double x, const double y) {
    // Touch mode: while a contact is down, its position is sent straight
    // from the window system's callback instead of waiting for the next
    // rendered frame (live_pointer() only starts and ends the contact) — the
    // same reasoning as mouse mode's relative motion below, and what fixed
    // its own drag feel. Scoped to the Live tab like live_pointer() itself:
    // live_phone_min_/max_ are only kept current by draw_live_surface(),
    // which only runs while that tab (fullscreen included, since it never
    // leaves Tab::live) is the one being drawn.
    if (live_touching_ && live_.touch && engine_.live_active() && tab_ == Tab::live) {
        const aoap::ProfileSetup setup = engine_.connected_setup();
        const ImVec2 size(live_phone_max_.x - live_phone_min_.x,
                          live_phone_max_.y - live_phone_min_.y);
        if (setup.touch.enabled && size.x > 0.0f && size.y > 0.0f) {
            const ImVec2 preview(
                std::clamp((static_cast<float>(x) - live_phone_min_.x) / size.x, 0.0f, 1.0f),
                std::clamp((static_cast<float>(y) - live_phone_min_.y) / size.y, 0.0f, 1.0f));
            const ImVec2 device = live_to_device(preview);
            const int32_t max_x = std::max(1, setup.touch.width - 1);
            const int32_t max_y = std::max(1, setup.touch.height - 1);
            const int32_t tx = std::clamp(
                static_cast<int32_t>(std::lround(device.x * static_cast<float>(max_x))), 0, max_x);
            const int32_t ty = std::clamp(
                static_cast<int32_t>(std::lround(device.y * static_cast<float>(max_y))), 0, max_y);
            if (tx != live_touch_x_ || ty != live_touch_y_) {
                live_touch_x_ = tx;
                live_touch_y_ = ty;
                engine_.live_send(aoap::TouchEvent{live_finger(), true, tx, ty});
            }
        }
    }

    // Only needed while the pointer is captured (see live_pointer() and
    // frame()); otherwise ImGui's own io.MouseDelta is used as before, so
    // this stays a no-op the rest of the time and adds no per-frame cost.
    // live_mouse_captured_ is only ever true once live_pointer() has already
    // checked live_.mouse && setup.mouse.enabled, so it is safe to send here.
    if (!live_mouse_captured_) {
        live_raw_cursor_valid_ = false;
        return;
    }
    if (live_raw_cursor_valid_) {
        // Sent as it arrives, straight from the window system's callback,
        // instead of accumulating for the next rendered frame to drain: a
        // drag's feel depends on this not waiting on render/vsync cadence.
        // The fractional remainder is kept in live_move_x_/y_ so slow
        // movement is not lost to truncation.
        live_move_x_ += x - live_raw_cursor_x_;
        live_move_y_ += y - live_raw_cursor_y_;
        const auto whole = [](double& value) {
            const double truncated = std::trunc(value);
            value -= truncated;
            return static_cast<int32_t>(std::clamp(truncated, -32767.0, 32767.0));
        };
        const int32_t dx = whole(live_move_x_);
        const int32_t dy = whole(live_move_y_);
        if (dx != 0 || dy != 0)
            engine_.live_send(aoap::MouseMove{dx, dy});
    }
    live_raw_cursor_x_ = x;
    live_raw_cursor_y_ = y;
    live_raw_cursor_valid_ = true;
}

void App::on_search_shortcut() {
    if (engine_.phase() == Phase::playing)
        return;
    tab_ = Tab::player;
    open_picker_ = true;
}

void App::on_paste_shortcut() {
    if (tab_ == Tab::live)
        live_paste_clipboard();
}

// --- Frame -----------------------------------------------------------------

void App::frame() {
    poll();
    ui::begin_frame_animations();

    ImGuiIO& io = ImGui::GetIO();
    // Mouse mode's captured pointer is invisible and moves without limit
    // (see live_capture_pointer()), so io.MousePos would otherwise
    // eventually drift onto some other button in this window and click it —
    // the whole point of the capture is that the pointer belongs to the
    // preview alone. Pinning it off-screen (ImGui's own "no mouse" sentinel)
    // makes every button and tab in this window correctly see no hover and
    // no click for as long as the capture lasts; live_pointer() tracks the
    // actual motion itself instead, from on_cursor()'s raw deltas.
    if (live_mouse_captured_) {
        constexpr ImVec2 off_screen(-FLT_MAX, -FLT_MAX);
        io.MousePos = off_screen;
        io.MousePosPrev = off_screen;
    }
    const bool popup_open = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const int pressed_key = std::exchange(pending_player_key_, 0);
    // The frame after a pick ends is skipped too, so the key that ended it
    // (Esc cancelling) does not also run its default action.
    const bool picking_keys = player_key_picking_ != PlayerAction::none || player_key_was_picking_;
    player_key_was_picking_ = player_key_picking_ != PlayerAction::none;
    if (tab_ == Tab::player && !io.WantTextInput && !popup_open && !picking_keys) {
        // A remapped action answers to its one key only; an unset one keeps
        // its default keys, including the extra Ctrl+S and Backspace.
        const int play_key = player_keys_[0];
        const int stop_key = player_keys_[1];
        const int restart_key = player_keys_[2];
        if (play_key != 0 ? pressed_key == play_key : ImGui::IsKeyPressed(ImGuiKey_Space, false))
            toggle_playback();
        if (engine_.phase() == Phase::playing &&
            (stop_key != 0 ? pressed_key == stop_key
                           : ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                                 (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))))
            stop_playback();
        if (!script_path_.empty() &&
            (restart_key != 0 ? pressed_key == restart_key
                              : ImGui::IsKeyPressed(ImGuiKey_Home, false) ||
                                    ImGui::IsKeyPressed(ImGuiKey_Backspace, false)))
            seek({});
        if (engine_.phase() == Phase::playing && io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
                engine_.player().adjust_offset_ns(-ns_per_ms);
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
                engine_.player().adjust_offset_ns(ns_per_ms);
        }
    }
    const bool forwarding_keys = engine_.live_active() && live_.key;
    // The pointer capture's own release key is handled in on_key(), straight
    // from the window system, so it works whichever key is configured and
    // whether or not Keyboard forwarding is on.
    // Live control owns the keyboard while it is forwarding keys.
    if (forwarding_keys)
        io.ClearInputKeys();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(20), px(16)));
    constexpr ImGuiWindowFlags root_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##root", nullptr, root_flags);
    ImGui::PopStyleVar();

    if (live_fullscreen_) {
        // Everything but the preview and its switches is out of the way.
        draw_live_fullscreen();
        if (!ImGui::IsAnyItemActive())
            persist_settings();
        ImGui::End();
        return;
    }

    draw_header();
    gap(4);

    const float log_height = log_open_ ? px(136) : ImGui::GetFrameHeight() + px(30);
    const float body = std::max(ImGui::GetContentRegionAvail().y - log_height -
                                    ImGui::GetStyle().ItemSpacing.y - px(4),
                                px(120));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    const float sidebar_shown_width = sidebar_collapsed_ ? px(44) : px(sidebar_width_);
    ImGui::BeginChild("##sidebar", ImVec2(sidebar_shown_width, body), ImGuiChildFlags_None);
    if (sidebar_collapsed_)
        draw_sidebar_collapsed();
    else
        draw_sidebar();
    ImGui::EndChild();
    if (sidebar_collapsed_) {
        ImGui::SameLine(0, px(12));
    } else {
        ImGui::SameLine(0, px(6));
        draw_sidebar_splitter(body);
        ImGui::SameLine(0, px(6));
    }
    ImGui::BeginChild("##navrail", ImVec2(px(52), body), ImGuiChildFlags_None);
    draw_nav_rail();
    ImGui::EndChild();
    ImGui::SameLine(0, px(10));
    ImGui::BeginChild("##main", ImVec2(0, body), ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    {
        if (tab_ == Tab::player)
            draw_player();
        else if (tab_ == Tab::live)
            draw_live();
        else if (tab_ == Tab::playlist)
            draw_playlist();
        else
            draw_recorder();
    }
    ImGui::EndChild();

    gap(4);
    draw_log(log_height);

    // Saved once an edit is finished, not on every keystroke or drag step.
    if (!ImGui::IsAnyItemActive())
        persist_settings();
    ImGui::End();
}

} // namespace gui
