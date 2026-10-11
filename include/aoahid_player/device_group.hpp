// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "device.hpp"
#include "event_script.hpp"
#include "events.hpp"
#include "spec_builder.hpp"

namespace aoap {

// Which profile a script row drives.
Profile profile_of(const EventPayload& payload) noexcept;

// Point-in-time copy of one device's state, safe to take from any thread.
struct DeviceStatus {
    std::string label;
    std::string key; // DeviceEntry::key of the entry it was opened from
    uint64_t reports{};
    uint64_t errors{};
    uint64_t skipped{}; // rows it refused as outside its own settings
    bool active{};
    std::string last_error;
    uint16_t adb_port{}; // the ADB Bridge's port, 0 when it was not started
    uint32_t profiles{}; // the profiles it has a Node for now
    ProfileSetup setup;  // what those Nodes were opened with
};

// Holds every connected Device and sends one script event to each device
// that has a Node for it. Per device:
//   1. mutate in-memory state (aoahid_touchscreen_contact / aoahid_keyboard_key / ... - cheap)
//   2. submit (non-blocking)
// Every device is submitted before any completion is awaited, so the total
// wait is bounded by the slowest device rather than the sum of all of them.
// The submissions stay on one thread because libaoahid requires the caller
// to serialize every call touching one Context.
//
// A device that fails (unplugged, timed out, ...) is closed and skipped from
// then on instead of aborting playback; the sink is told once, in plain words.
//
// Threading: add_device / reset / apply / flush / reconfigure belong to the one
// thread that owns the Context. snapshot() and the counters may be read from any thread.
class DeviceGroup {
  public:
    // Failure deadline for one report's completion drain. It bounds a stuck
    // transfer; it is not a delay added to a successful send.
    static constexpr uint32_t drain_deadline_ms = 500U;

    explicit DeviceGroup(EventSink* sink = nullptr) noexcept : sink_(sink) {}
    ~DeviceGroup();

    DeviceGroup(const DeviceGroup&) = delete;
    DeviceGroup& operator=(const DeviceGroup&) = delete;
    DeviceGroup(DeviceGroup&&) = delete;
    DeviceGroup& operator=(DeviceGroup&&) = delete;

    // `setup` is what `device` was opened with (see open_profiles()).
    void add_device(Device&& device, std::string key, const ProfileSetup& setup);

    // Closes and forgets every device.
    void reset() noexcept;

    [[nodiscard]] bool empty() const noexcept { return slots_.empty(); }
    [[nodiscard]] size_t size() const noexcept { return slots_.size(); }
    [[nodiscard]] size_t active_count() const noexcept {
        return active_count_.load(std::memory_order_relaxed);
    }
    // The profiles at least one active device has a Node for.
    [[nodiscard]] uint32_t profile_mask() const noexcept {
        return profile_mask_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool dirty() const noexcept { return dirty_mask_ != 0U; }

    // Applies one script row to every active device that has its profile.
    //   AOAHID_ERR_BUSY        the row would overwrite an edge that has not
    //                          reached a report yet and nothing was changed;
    //                          the caller flushes and retries.
    //   AOAHID_ERR_UNSUPPORTED no active device has a Node for that profile.
    //   AOAHID_ERR_PARAM       every device that has the profile refused the
    //                          row as outside its Spec.
    // A device that refuses a row another one took skips it and counts it
    // (DeviceStatus::skipped). Any other failure drops just that device.
    // `space` is the space the row's touch and pen positions are in; each
    // device gets them scaled to its own surface.
    aoahid_result apply(const EventPayload& payload, Space space);

    // Brings the device at `index` (snapshot() order) to `wanted`: closes the
    // Nodes that are no longer wanted or whose settings changed, then opens
    // the missing ones. Settings that fail validation are refused before any
    // Node is touched. A Node that cannot be closed drops the device; one
    // that cannot be opened stays off and is named in `error`. Returns the
    // profiles that were opened, so the caller can wait for Android to
    // register them. Not while a script is playing.
    uint32_t reconfigure(size_t index, ProfileSetup wanted, std::string& error);

    // Sends the battery level of every HID that has one and has not sent this
    // level yet, on one device or (SIZE_MAX) on all. Call it once Android has
    // registered the HIDs: a report sent earlier is refused.
    void send_battery_levels(size_t index = SIZE_MAX);

    // Adds wheel detents on every active mouse; results as for apply().
    aoahid_result scroll(int32_t wheel);

    // Toggles a Consumer Control usage on every active toggle profile.
    aoahid_result toggle(uint16_t usage, uint8_t value);

    [[nodiscard]] uint64_t rejected_rows() const noexcept { return rejected_rows_; }
    [[nodiscard]] const std::string& rejection_detail() const noexcept {
        return rejection_detail_;
    }

    // Submits every dirty node on every active device, then drains them.
    // Returns true when anything was sent.
    bool flush();

    [[nodiscard]] std::vector<DeviceStatus> snapshot() const;

    // The ADB Bridge of the device at `index` (snapshot() order); see
    // Device::start_adb_bridge(). Start returns -1, the proxy's null-argument
    // code, for an unknown or dropped device. Stopping can block up to ~1 s,
    // so not while a script plays.
    int start_adb_bridge(size_t index, uint16_t port);
    void stop_adb_bridge(size_t index) noexcept;

  private:
    struct Slot {
        Device device;
        std::string label;
        std::string key;
        ProfileSetup setup; // what is registered now; guarded by mutex_
        Space touch;        // where positions are scaled to; worker thread
        Space pen;
        Space digitizer;
        std::atomic<uint32_t> profiles{};
        // The level each HID last sent; INT32_MIN before the first. Worker thread.
        std::array<int32_t, profile_count> battery_sent{};
        std::atomic<uint64_t> skipped{};
        std::atomic<uint16_t> adb_port{};
        std::string last_error; // guarded by mutex_
        std::atomic<uint64_t> reports{};
        std::atomic<uint64_t> errors{};
        std::atomic<bool> active{true};
    };

    // Counters have one writer, so a plain load/store pair avoids a locked
    // read-modify-write on the send path.
    static void bump(std::atomic<uint64_t>& counter) noexcept {
        counter.store(counter.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }

    void fail(Slot& slot, aoahid_result result);
    template <typename Mutate> aoahid_result broadcast(Profile profile, Mutate&& mutate_one);
    void refresh_mask() noexcept;
    static void set_surfaces(Slot& slot) noexcept;

    std::vector<std::unique_ptr<Slot>> slots_;
    mutable std::mutex mutex_; // guards slots_ layout and last_error for snapshot()
    EventSink* sink_;
    std::atomic<size_t> active_count_{};
    std::atomic<uint32_t> profile_mask_{};
    uint32_t dirty_mask_{};
    uint64_t rejected_rows_{};
    std::string rejection_detail_;
};

// Opens a Node on `device` for every enabled profile of `setup` it does not
// have yet. A profile that cannot be registered is switched off in `setup`
// and explained in `error`. Returns the profiles that were opened.
uint32_t open_profiles(Device& device, ProfileSetup& setup, std::string& error);

} // namespace aoap
