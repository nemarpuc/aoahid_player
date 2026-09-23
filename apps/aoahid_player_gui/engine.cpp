// SPDX-License-Identifier: MIT
#include "engine.hpp"

#include "aoahid_player/input_state.hpp"
#include "aoahid_player/timing.hpp"

#include <algorithm>
#include <cstdio>
#include <type_traits>

namespace gui {
namespace {

constexpr size_t live_inbox_limit = 4096;

std::string plural(const uint64_t count, const char* one, const char* many) {
    return std::to_string(count) + ' ' + (count == 1 ? one : many);
}

} // namespace

Engine::Engine(aoap::EventSink& sink, std::function<void()> wake)
    : sink_(sink), wake_(std::move(wake)), session_(&sink), player_(session_.group(), &sink) {
    player_.set_observer(this);
    // Wired once, unconditionally: pump_live() itself is a cheap no-op
    // whenever Live control is off and nothing is left to release, so a
    // script that never turns Live on pays only one relaxed load per wake.
    player_.set_live_pump([this] { pump_live(); });
    live_inbox_.reserve(256);
    worker_ = std::thread([this] { loop(); });
}

Engine::~Engine() {
    {
        const std::lock_guard lock(mutex_);
        queue_.clear();
        playlist_abort_ = true;
        live_stop_ = true;
        if (running_play_)
            player_.stop();
        queue_.push_back(Command{Command::Kind::quit, {}, {}, {}, {}, {}, 0, {}, 0});
    }
    ready_.notify_one();
    if (worker_.joinable())
        worker_.join();
}

void Engine::note(const aoap::Severity severity, const std::string& text) {
    sink_.message(severity, text);
}

void Engine::set_phase(const Phase phase) noexcept {
    phase_.store(phase, std::memory_order_release);
    if (wake_)
        wake_();
}

void Engine::push(Command command, const Phase optimistic) {
    {
        const std::lock_guard lock(mutex_);
        queue_.push_back(std::move(command));
        // Shown at once, so a double click cannot queue the same step twice.
        phase_.store(optimistic, std::memory_order_release);
    }
    ready_.notify_one();
}

void Engine::refresh() {
    if (phase() != Phase::idle)
        return;
    push(Command{Command::Kind::refresh, {}, {}, {}, {}, {}, 0, {}, 0}, Phase::refreshing);
}

void Engine::connect(std::vector<size_t> selection, aoap::ProfileSetup setup) {
    if (phase() != Phase::idle)
        return;
    push(Command{Command::Kind::connect, std::move(selection), std::move(setup), {}, {}, {}, 0,
                 {}, 0},
         Phase::connecting);
}

void Engine::accessory(std::vector<size_t> selection) {
    if (selection.empty() || busy())
        return;
    Command command{};
    command.kind = Command::Kind::accessory;
    command.selection = std::move(selection);
    push(std::move(command), Phase::connecting);
}

void Engine::disconnect() {
    if (!connected())
        return;
    live_active_.store(false, std::memory_order_release);
    {
        const std::lock_guard lock(mutex_);
        queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                    [](const Command& c) {
                                        return c.kind == Command::Kind::play ||
                                               c.kind == Command::Kind::playlist ||
                                               c.kind == Command::Kind::live;
                                    }),
                     queue_.end());
        playlist_abort_ = true;
        live_stop_ = true; // exits run_live() if it is the one currently running
        if (running_play_)
            player_.stop();
    }
    ready_.notify_one();
    player_.wake_live();
    push(Command{Command::Kind::disconnect, {}, {}, {}, {}, {}, 0, {}, 0}, Phase::disconnecting);
}

void Engine::play(std::shared_ptr<const aoap::EventScript> script, std::string name,
                  const aoap::PlaybackPosition start, const int64_t loops) {
    // Live control, if on, keeps running alongside playback (see
    // pump_live()); it is not stopped here.
    if (phase() != Phase::connected || !script)
        return;
    push(Command{Command::Kind::play, {}, {}, std::move(script), std::move(name), start, loops,
                 {}, 0},
         Phase::playing);
}

void Engine::play_playlist(std::vector<PlaylistStep> steps, const int64_t time_limit_ns) {
    if (phase() != Phase::connected || steps.empty())
        return;
    {
        const std::lock_guard lock(mutex_);
        playlist_abort_ = false;
    }
    push(Command{Command::Kind::playlist, {}, {}, {}, {}, {}, 0, std::move(steps),
                 time_limit_ns},
         Phase::playing);
}

void Engine::stop() {
    const std::lock_guard lock(mutex_);
    const auto queued = std::find_if(queue_.begin(), queue_.end(), [](const Command& c) {
        return c.kind == Command::Kind::play || c.kind == Command::Kind::playlist;
    });
    if (queued != queue_.end()) {
        queue_.erase(queued);
        if (!running_play_)
            phase_.store(Phase::connected, std::memory_order_release);
    }
    playlist_abort_ = true;
    if (running_play_)
        player_.stop();
}

void Engine::live_start() {
    const Phase now = phase();
    if (now != Phase::connected && now != Phase::playing)
        return;
    {
        const std::lock_guard lock(mutex_);
        live_stop_ = false;
        live_inbox_.clear();
    }
    live_active_.store(true, std::memory_order_release);
    if (now != Phase::connected) {
        // Playback already owns the worker thread; its own live pump (see
        // Player::set_live_pump()) picks up live_inbox_ from here, so there
        // is nothing to queue.
        player_.wake_live();
        return;
    }
    push(Command{Command::Kind::live, {}, {}, {}, {}, {}, 0, {}, 0}, Phase::connected);
}

void Engine::live_stop() {
    live_active_.store(false, std::memory_order_release);
    {
        const std::lock_guard lock(mutex_);
        live_stop_ = true; // exits run_live() if it is the one currently running
        const auto queued = std::find_if(queue_.begin(), queue_.end(), [](const Command& c) {
            return c.kind == Command::Kind::live;
        });
        if (queued != queue_.end())
            queue_.erase(queued); // never started; nothing was pressed yet
    }
    ready_.notify_one();
    player_.wake_live(); // in case a script is running and its pump is waiting
}

void Engine::live_send(const aoap::EventPayload& payload) {
    if (!live_active_.load(std::memory_order_relaxed))
        return;
    {
        const std::lock_guard lock(mutex_);
        // Bursts are folded while the worker is busy with a report: moves add
        // up, and the latest contact position or axis value wins. Edges
        // (presses, releases, touch down and up) always keep their order.
        if (!live_inbox_.empty() && !live_inbox_.back().wheel) {
            aoap::EventPayload& last = live_inbox_.back().payload;
            if (const auto* move = std::get_if<aoap::MouseMove>(&payload)) {
                if (auto* previous = std::get_if<aoap::MouseMove>(&last)) {
                    previous->dx += move->dx;
                    previous->dy += move->dy;
                    live_pending_.store(true, std::memory_order_release);
                    ready_.notify_one();
                    player_.wake_live();
                    return;
                }
            } else if (const auto* touch = std::get_if<aoap::TouchEvent>(&payload)) {
                auto* previous = std::get_if<aoap::TouchEvent>(&last);
                if (previous != nullptr && touch->state && previous->state &&
                    previous->finger_id == touch->finger_id) {
                    *previous = *touch;
                    live_pending_.store(true, std::memory_order_release);
                    ready_.notify_one();
                    player_.wake_live();
                    return;
                }
            } else if (const auto* axis = std::get_if<aoap::GamepadAxis>(&payload)) {
                auto* previous = std::get_if<aoap::GamepadAxis>(&last);
                if (previous != nullptr && previous->axis_index == axis->axis_index) {
                    previous->value = axis->value;
                    live_pending_.store(true, std::memory_order_release);
                    ready_.notify_one();
                    player_.wake_live();
                    return;
                }
            }
        }
        if (live_inbox_.size() >= live_inbox_limit)
            return;
        LiveItem item{};
        item.payload = payload;
        live_inbox_.push_back(item);
    }
    live_pending_.store(true, std::memory_order_release);
    ready_.notify_one();
    player_.wake_live();
}

void Engine::live_scroll(const int32_t wheel) {
    if (wheel == 0 || !live_active_.load(std::memory_order_relaxed))
        return;
    {
        const std::lock_guard lock(mutex_);
        if (!live_inbox_.empty() && live_inbox_.back().wheel) {
            live_inbox_.back().wheel_delta += wheel;
        } else {
            if (live_inbox_.size() >= live_inbox_limit)
                return;
            LiveItem item{};
            item.wheel = true;
            item.wheel_delta = wheel;
            live_inbox_.push_back(item);
        }
    }
    live_pending_.store(true, std::memory_order_release);
    ready_.notify_one();
    player_.wake_live();
}

void Engine::live_toggle(const uint16_t usage, const uint8_t value) {
    if (!live_active_.load(std::memory_order_relaxed))
        return;
    {
        const std::lock_guard lock(mutex_);
        if (live_inbox_.size() >= live_inbox_limit)
            return;
        LiveItem item{};
        item.toggle = true;
        item.usage = usage;
        item.toggle_value = value;
        live_inbox_.push_back(item);
    }
    live_pending_.store(true, std::memory_order_release);
    ready_.notify_one();
    player_.wake_live();
}

std::vector<aoap::DeviceEntry> Engine::devices() const {
    const std::lock_guard lock(mutex_);
    return devices_;
}

uint64_t Engine::devices_version() const {
    const std::lock_guard lock(mutex_);
    return devices_version_;
}

std::vector<aoap::DeviceStatus> Engine::device_status() const {
    return session_.group().snapshot();
}

aoap::ProfileSetup Engine::connected_setup() const {
    const std::lock_guard lock(mutex_);
    return setup_;
}

PlaylistProgress Engine::playlist_progress() const {
    const std::lock_guard lock(mutex_);
    return progress_;
}

// --- Sent events for the preview ---------------------------------------------

void Engine::observe(const Observed& event) noexcept {
    if (!observing_.load(std::memory_order_relaxed))
        return;
    const size_t head = ring_head_.load(std::memory_order_relaxed);
    const size_t next = (head + 1) % ring_size;
    if (next == ring_tail_.load(std::memory_order_acquire))
        return; // full: the preview drops this event rather than stall a send
    ring_[head] = event;
    ring_head_.store(next, std::memory_order_release);
}

bool Engine::take(Observed& out) noexcept {
    const size_t tail = ring_tail_.load(std::memory_order_relaxed);
    if (tail == ring_head_.load(std::memory_order_acquire))
        return false;
    out = ring_[tail];
    ring_tail_.store((tail + 1) % ring_size, std::memory_order_release);
    return true;
}

void Engine::sent(const aoap::EventPayload& payload) noexcept {
    if (!observing_.load(std::memory_order_relaxed))
        return;
    observe(Observed{Observed::Kind::payload, payload, 0, aoap::Timing::now_ns()});
}

// --- Worker --------------------------------------------------------------

std::shared_ptr<const aoap::EventScript>
Engine::prepare(const std::shared_ptr<const aoap::EventScript>& script) {
    const aoap::ProfileSetup& setup = session_.setup();
    auto scaled = std::make_shared<aoap::EventScript>();
    const bool touch = setup.touch.enabled;
    const bool pen = setup.pen.enabled;
    if (!aoap::scale_script(*script, touch ? setup.touch.width : 0, touch ? setup.touch.height : 0,
                            pen ? setup.pen.width : 0, pen ? setup.pen.height : 0, *scaled))
        return script;
    char text[128];
    std::snprintf(text, sizeof text, "Coordinates scaled from %dx%d to the connected surface.",
                  script->screen_width, script->screen_height);
    note(aoap::Severity::info, text);
    return scaled;
}

void Engine::loop() {
    while (true) {
        Command command;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [this] { return !queue_.empty(); });
            command = std::move(queue_.front());
            queue_.pop_front();
            if (command.kind == Command::Kind::quit)
                break;
            running_play_ =
                command.kind == Command::Kind::play || command.kind == Command::Kind::playlist;
        }
        execute(command);
        {
            const std::lock_guard lock(mutex_);
            running_play_ = false;
        }
        if (wake_)
            wake_();
    }
    // Leaving: release whatever is held and unregister every HID device.
    session_.disconnect();
}

// After a run, the phase goes back to connected unless a queued command has
// already claimed it (a disconnect, another play). If Live control is still
// on, it lost the worker thread's low-latency wait the moment playback
// preempted it (see run_live()), so a fresh live command resumes it.
void Engine::finish_run() {
    {
        const std::lock_guard lock(mutex_);
        if (queue_.empty()) {
            phase_.store(Phase::connected, std::memory_order_release);
            if (live_active_.load(std::memory_order_relaxed)) {
                live_stop_ = false;
                queue_.push_back(Command{Command::Kind::live, {}, {}, {}, {}, {}, 0, {}, 0});
            }
        }
    }
    ready_.notify_one();
    if (wake_)
        wake_();
}

void Engine::apply_live_one(const aoap::EventPayload& payload) {
    aoap::DeviceGroup& group = session_.group();
    aoahid_result result = group.apply(payload);
    if (result == AOAHID_ERR_BUSY) {
        // The opposite edge of the same control is still unreported.
        group.flush();
        result = group.apply(payload);
    }
    if (result == AOAHID_OK) {
        live_held_.apply(payload);
        sent(payload);
    }
}

void Engine::release_live() {
    std::vector<aoap::EventPayload> releases;
    std::vector<aoap::EventPayload> presses;
    aoap::InputState::transition(live_held_, aoap::InputState{}, releases, presses);
    if (releases.empty())
        return;
    // Releases in their own report.
    for (const aoap::EventPayload& payload : releases)
        apply_live_one(payload);
    session_.group().flush();
}

// Reusable on any thread that currently owns the DeviceGroup: run_live()'s
// own loop when nothing else is running, and Player's live pump (see
// Player::set_live_pump()) while a script plays. Cheap to call when there is
// nothing to do — one relaxed load — so both call it liberally.
void Engine::pump_live() {
    if (!live_active_.load(std::memory_order_acquire)) {
        // Just turned off (or never on): release whatever is still held,
        // exactly once — neutral() is false only until this runs.
        if (!live_held_.neutral())
            release_live();
        return;
    }
    if (!live_pending_.exchange(false, std::memory_order_acq_rel))
        return; // nothing queued since the last drain
    std::vector<LiveItem> batch;
    {
        const std::lock_guard lock(mutex_);
        batch.swap(live_inbox_);
    }
    if (batch.empty())
        return;
    aoap::DeviceGroup& group = session_.group();
    for (const LiveItem& item : batch) {
        if (item.wheel) {
            aoahid_result result = group.scroll(item.wheel_delta);
            if (result == AOAHID_ERR_BUSY) {
                group.flush();
                result = group.scroll(item.wheel_delta);
            }
            if (result == AOAHID_OK)
                observe(Observed{Observed::Kind::wheel, aoap::MouseMove{0, 0}, item.wheel_delta,
                                 aoap::Timing::now_ns()});
            continue;
        }
        if (item.toggle) {
            aoahid_result result = group.toggle(item.usage, item.toggle_value);
            if (result == AOAHID_ERR_BUSY) {
                group.flush();
                group.toggle(item.usage, item.toggle_value);
            }
            continue;
        }
        apply_live_one(item.payload);
    }
    // One report per burst; while it drains, new input folds in the inbox.
    group.flush();
}

void Engine::run_live() {
    aoap::DeviceGroup& group = session_.group();
    bool stopped = false;
    while (true) {
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [&] {
                return live_stop_ || !queue_.empty() ||
                       live_pending_.load(std::memory_order_relaxed);
            });
            if (live_stop_ || !queue_.empty()) {
                // Captured under the same lock as the check above, not by
                // looking at what ended up queued: a live_start() racing
                // right after this (a fast toggle off/on) always resets
                // live_stop_ before it queues its own fresh live command,
                // so reading it here — rather than peeking at queue_'s
                // front afterward — cannot mistake that restart for a stop.
                stopped = live_stop_;
                break; // stopped, or a play/playlist/disconnect takes over
            }
        }
        pump_live();
        if (group.active_count() == 0) {
            note(aoap::Severity::error, "Every device stopped responding; live control stopped.");
            stopped = true;
            break;
        }
    }

    // A play or playlist command taking over (queue_ became non-empty
    // without live_stop_) keeps Live running through it — its own live pump
    // inherits live_held_/live_inbox_ as they are; a genuine stop releases
    // everything Live pressed.
    if (stopped) {
        live_active_.store(false, std::memory_order_release);
        release_live();
    }
}

void Engine::execute(Command& command) {
    std::string error;
    switch (command.kind) {
    case Command::Kind::refresh: {
        std::vector<aoap::DeviceEntry> found;
        const bool ok = session_.refresh(found, error);
        const size_t count = found.size();
        {
            const std::lock_guard lock(mutex_);
            devices_ = std::move(found);
            ++devices_version_;
        }
        if (!ok)
            note(aoap::Severity::error, error);
        else if (count == 0)
            note(aoap::Severity::warning,
                 "No AOA-capable device found. Use a data cable, set the phone's USB mode to "
                 "something other than \"Charge only\", and on Linux install the udev rule.");
        else
            note(aoap::Severity::info, "Found " + plural(count, "device.", "devices."));
        set_phase(Phase::idle);
        break;
    }
    case Command::Kind::connect: {
        if (!session_.connect(command.selection, command.setup, error)) {
            note(aoap::Severity::error, error);
            profiles_.store(0U, std::memory_order_relaxed);
            set_phase(Phase::idle);
            break;
        }
        {
            const std::lock_guard lock(mutex_);
            setup_ = session_.setup();
        }
        setup_version_.fetch_add(1, std::memory_order_acq_rel);
        profiles_.store(session_.group().profile_mask(), std::memory_order_relaxed);
        note(aoap::Severity::info,
             "Ready: " + plural(session_.group().size(), "device", "devices") + " with " +
                 aoap::describe_profiles(session_.group().profile_mask()) + ".");
        set_phase(Phase::connected);
        break;
    }
    case Command::Kind::accessory: {
        session_.disconnect();
        
        bool success = session_.accessory(command.selection, error);
        if (!success && !error.empty()) {
            note(aoap::Severity::error, error);
        }
        if (success) {
            // Re-fetch list to update UI, but the device will disconnect soon
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        Command cmd{};
        cmd.kind = Command::Kind::refresh;
        push(std::move(cmd), Phase::refreshing);
        set_phase(Phase::idle);
        break;
    }
    case Command::Kind::disconnect:
        session_.disconnect();
        profiles_.store(0U, std::memory_order_relaxed);
        note(aoap::Severity::info, "Disconnected.");
        set_phase(Phase::idle);
        break;
    case Command::Kind::play: {
        if (!session_.connected()) {
            set_phase(Phase::idle);
            break;
        }
        player_.set_stop_at(0);
        // The live offset belongs to one run: it starts at zero every time.
        player_.set_offset_ns(0);
        player_.set_loop_limit(command.loops);
        player_.load(prepare(command.script));
        note(aoap::Severity::info, "Playing " + command.name + ".");
        player_.run(command.start);
        const aoap::PlaybackStatus status = player_.status();
        note(aoap::Severity::info, "Stopped after " + plural(status.loops, "loop", "loops") +
                                       " (" + plural(status.reports, "report", "reports") + ").");
        finish_run();
        break;
    }
    case Command::Kind::playlist: {
        if (!session_.connected()) {
            set_phase(Phase::idle);
            break;
        }
        const int64_t stop_at =
            command.time_limit_ns > 0 ? aoap::Timing::now_ns() + command.time_limit_ns : 0;
        player_.set_stop_at(stop_at);
        const size_t count = command.steps.size();
        bool by_time = false;
        size_t finished = 0;
        for (size_t index = 0; index < count; ++index) {
            const PlaylistStep& step = command.steps[index];
            {
                const std::lock_guard lock(mutex_);
                if (playlist_abort_)
                    break;
                progress_ = PlaylistProgress{true, index, count, step.name, step.loops, stop_at};
            }
            if (wake_)
                wake_();
            player_.set_offset_ns(0);
            player_.set_loop_limit(std::max<int64_t>(step.loops, 1));
            player_.load(prepare(step.script));
            note(aoap::Severity::info, "Playlist " + std::to_string(index + 1) + "/" +
                                           std::to_string(count) + ": " + step.name + " (" +
                                           plural(static_cast<uint64_t>(step.loops), "loop",
                                                  "loops") +
                                           ").");
            player_.run({});
            if (player_.stopped_by_time()) {
                by_time = true;
                break;
            }
            if (session_.group().active_count() == 0)
                break;
            const std::lock_guard lock(mutex_);
            if (playlist_abort_)
                break;
            ++finished;
        }
        player_.set_stop_at(0);
        {
            const std::lock_guard lock(mutex_);
            progress_ = PlaylistProgress{};
        }
        if (by_time)
            note(aoap::Severity::info, "Time limit reached; the playlist stopped.");
        else if (finished == count)
            note(aoap::Severity::info, "Playlist finished.");
        else
            note(aoap::Severity::info, "Playlist stopped.");
        finish_run();
        break;
    }
    case Command::Kind::live:
        if (!session_.connected()) {
            set_phase(Phase::idle);
            break;
        }
        run_live();
        finish_run();
        break;
    case Command::Kind::quit:
        break;
    }
}

} // namespace gui
