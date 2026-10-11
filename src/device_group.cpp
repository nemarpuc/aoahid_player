// SPDX-License-Identifier: MIT
#include "aoahid_player/device_group.hpp"

#include "aoahid_player/timing.hpp"

#include <aoahid.hpp>

#include <iterator>
#include <type_traits>
#include <utility>

namespace aoap {

Profile profile_of(const EventPayload& payload) noexcept {
    return std::visit(
        [](const auto& value) noexcept {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, TouchEvent>)
                return Profile::touch;
            else if constexpr (std::is_same_v<T, MouseMove> || std::is_same_v<T, MouseButton>)
                return Profile::mouse;
            else if constexpr (std::is_same_v<T, KeyEvent>)
                return Profile::key;
            else if constexpr (std::is_same_v<T, PenSample>)
                return Profile::pen;
            else if constexpr (std::is_same_v<T, DigitizerSample>)
                return Profile::digitizer;
            else if constexpr (std::is_same_v<T, MediaKey>)
                return Profile::toggle;
            else
                return Profile::gamepad;
        },
        payload);
}

namespace {

// The pen and the digitizer take samples with the same fields (`Out` is the
// one of the profile); `surface` is the one of the profile the sample is for.
template <typename Out, typename Sample>
Out stylus_sample(const Sample& value, const Space space, const Space surface) {
    Out sample{};
    sample.in_range = value.in_range ? 1U : 0U;
    sample.tip = value.tip ? 1U : 0U;
    sample.x = value.x;
    sample.y = value.y;
    if (space.width > 0 && space != surface) {
        sample.x = scale_coordinate(sample.x, space.width, surface.width);
        sample.y = scale_coordinate(sample.y, space.height, surface.height);
    }
    sample.pressure = value.pressure;
    return sample;
}

// Applies one payload to one device, with its touch, pen, and digitizer
// positions scaled from `space` onto the device's own surfaces. Every
// mutation is in-memory only and the caller has already checked that the
// profile is present.
aoahid_result mutate(const Device& device, const EventPayload& payload, const Space space,
                     const Space touch, const Space pen, const Space digitizer) {
    return std::visit(
        [&](const auto& value) -> aoahid_result {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, TouchEvent>) {
                int32_t x = value.x;
                int32_t y = value.y;
                if (space.width > 0 && space != touch) {
                    x = scale_coordinate(x, space.width, touch.width);
                    y = scale_coordinate(y, space.height, touch.height);
                }
                return aoahid::touchscreen_contact(device.node(Profile::touch),
                                                   static_cast<uint32_t>(value.finger_id),
                                                   value.state, x, y, nullptr);
            } else if constexpr (std::is_same_v<T, MouseMove>) {
                return aoahid::mouse_move(device.node(Profile::mouse), value.dx, value.dy);
            } else if constexpr (std::is_same_v<T, MouseButton>) {
                // Both the CSV column and aoahid_mouse_button are one-based.
                return aoahid::mouse_button(device.node(Profile::mouse), value.button, value.pressed);
            } else if constexpr (std::is_same_v<T, KeyEvent>) {
                return aoahid::keyboard_key(device.node(Profile::key), value.usage, value.down);
            } else if constexpr (std::is_same_v<T, GamepadButton>) {
                return aoahid::gamepad_button(device.node(Profile::gamepad), value.button, value.pressed);
            } else if constexpr (std::is_same_v<T, GamepadAxis>) {
                return aoahid::gamepad_axis(device.node(Profile::gamepad), value.axis_index, value.value);
            } else if constexpr (std::is_same_v<T, GamepadDpad>) {
                return aoahid::gamepad_dpad(device.node(Profile::gamepad), value.up, value.down, value.right, value.left);
            } else if constexpr (std::is_same_v<T, MediaKey>) {
                // Each Consumer usage has its own bit; a release of a usage
                // that is not held changes nothing.
                return aoahid::toggle_usage(device.node(Profile::toggle), value.usage, value.down);
            } else if constexpr (std::is_same_v<T, PenSample>) {
                const auto sample = stylus_sample<aoahid_pen_sample>(value, space, pen);
                return aoahid::pen_update(device.node(Profile::pen), &sample);
            } else {
                const auto sample = stylus_sample<aoahid_digitizer_sample>(value, space, digitizer);
                return aoahid::digitizer_update(device.node(Profile::digitizer), &sample);
            }
        },
        payload);
}

} // namespace

DeviceGroup::~DeviceGroup() { reset(); }

void DeviceGroup::add_device(Device&& device, std::string key, const ProfileSetup& setup) {
    auto slot = std::make_unique<Slot>();
    slot->label = device.label();
    slot->key = std::move(key);
    slot->setup = setup;
    slot->battery_sent.fill(INT32_MIN);
    set_surfaces(*slot);
    slot->adb_port.store(device.adb_port(), std::memory_order_relaxed);
    slot->profiles.store(device.profile_mask(), std::memory_order_relaxed);
    slot->device = std::move(device);
    {
        const std::lock_guard lock(mutex_);
        slots_.push_back(std::move(slot));
    }
    active_count_.fetch_add(1, std::memory_order_relaxed);
    refresh_mask();
}

void DeviceGroup::set_surfaces(Slot& slot) noexcept {
    slot.touch = {slot.setup.touch.width, slot.setup.touch.height};
    slot.pen = {slot.setup.pen.width, slot.setup.pen.height};
    slot.digitizer = {slot.setup.digitizer.width, slot.setup.digitizer.height};
}

void DeviceGroup::refresh_mask() noexcept {
    uint32_t mask = 0;
    for (const std::unique_ptr<Slot>& slot : slots_) {
        if (slot->active.load(std::memory_order_relaxed))
            mask |= slot->profiles.load(std::memory_order_relaxed);
    }
    profile_mask_.store(mask, std::memory_order_relaxed);
}

void DeviceGroup::reset() noexcept {
    std::vector<std::unique_ptr<Slot>> closing;
    {
        const std::lock_guard lock(mutex_);
        closing.swap(slots_);
        profile_mask_.store(0U, std::memory_order_relaxed);
        active_count_.store(0U, std::memory_order_relaxed);
    }
    dirty_mask_ = 0U;
    rejected_rows_ = 0U;
    rejection_detail_.clear();
    // Closing drains and unregisters, which can block, so it runs without
    // the lock a UI thread may be waiting on in snapshot().
    for (const std::unique_ptr<Slot>& slot : closing)
        slot->device.close();
}

void DeviceGroup::fail(Slot& slot, const aoahid_result result) {
    bump(slot.errors);
    std::string detail = explain_error(result);
    const bool was_active = slot.active.exchange(false, std::memory_order_relaxed);
    {
        const std::lock_guard lock(mutex_);
        slot.last_error = detail;
    }
    if (!was_active)
        return;
    active_count_.fetch_sub(1, std::memory_order_relaxed);
    slot.device.close();
    slot.adb_port.store(0U, std::memory_order_relaxed);
    slot.profiles.store(0U, std::memory_order_relaxed);
    refresh_mask();
    if (sink_ == nullptr)
        return;
    if (result == AOAHID_ERR_STALL) {
        // A STALL is the phone refusing the report, not silence. Right after
        // connecting it usually means Android is still registering the HID.
        sink_->message(Severity::warning,
                       slot.label + " refused a report and was dropped. Right after connecting, "
                                    "Android may still be registering the device; start input "
                                    "about 100 ms after connecting. " +
                           detail);
    } else {
        sink_->message(Severity::warning,
                       slot.label + " stopped responding and was dropped. " + detail);
    }
}

// Runs `mutate_one` on every active device that has `profile`. See apply()
// for the results.
template <typename Mutate>
aoahid_result DeviceGroup::broadcast(const Profile profile, Mutate&& mutate_one) {
    bool present = false;
    bool applied = false;
    // Refusals are counted only once the row is known not to be retried.
    Slot* refused[16];
    size_t refused_count = 0;
    aoahid_result refusal = AOAHID_OK;
    for (const std::unique_ptr<Slot>& slot : slots_) {
        if (!slot->active.load(std::memory_order_relaxed) || !slot->device.has(profile))
            continue;
        present = true;
        const aoahid_result result = mutate_one(*slot);
        if (result == AOAHID_OK) {
            applied = true;
            continue;
        }
        if (result == AOAHID_ERR_BUSY) {
            if (!applied) {
                // Nothing has been applied yet, so the caller can flush and
                // retry without leaving the devices in different states.
                return AOAHID_ERR_BUSY;
            }
            // Another device already took this event, so retrying would
            // apply it twice there. This one is just behind on draining its
            // previous report, not unresponsive, so drop the event for it
            // alone instead of disconnecting it.
            continue;
        }
        if (result == AOAHID_ERR_PARAM || result == AOAHID_ERR_OVERFLOW) {
            // Outside what this device's Spec declared (OVERFLOW: more
            // contacts than its slots): a script or settings matter, so the
            // device is kept and skips the row.
            if (refused_count < std::size(refused))
                refused[refused_count++] = slot.get();
            refusal = result;
            continue;
        }
        fail(*slot, result);
    }
    for (size_t index = 0; index < refused_count; ++index)
        bump(refused[index]->skipped);
    if (refused_count != 0) {
        ++rejected_rows_;
        if (rejection_detail_.empty())
            rejection_detail_ = refused[0]->label + ": " + explain_error(refusal);
    }
    if (applied) {
        dirty_mask_ |= profile_bit(profile);
        return AOAHID_OK;
    }
    if (!present)
        return AOAHID_ERR_UNSUPPORTED;
    return refused_count != 0 ? AOAHID_ERR_PARAM : AOAHID_OK;
}

aoahid_result DeviceGroup::apply(const EventPayload& payload, const Space space) {
    return broadcast(profile_of(payload), [&](const Slot& slot) {
        return mutate(slot.device, payload, space, slot.touch, slot.pen, slot.digitizer);
    });
}

aoahid_result DeviceGroup::scroll(const int32_t wheel) {
    return broadcast(Profile::mouse, [&](const Slot& slot) {
        return aoahid::mouse_scroll(slot.device.node(Profile::mouse), wheel, 0);
    });
}

aoahid_result DeviceGroup::toggle(const uint16_t usage, const uint8_t value) {
    return broadcast(Profile::toggle, [&](const Slot& slot) {
        return aoahid::toggle_usage(slot.device.node(Profile::toggle), usage, value != 0);
    });
}

bool DeviceGroup::flush() {
    if (dirty_mask_ == 0U)
        return false;
    const uint32_t flushed = dirty_mask_;
    dirty_mask_ = 0U;

    for (const std::unique_ptr<Slot>& slot : slots_) {
        if (!slot->active.load(std::memory_order_relaxed))
            continue;
        aoahid_result failure = AOAHID_OK;
        for (size_t profile = 0; profile < profile_count; ++profile) {
            if ((flushed & (uint32_t{1} << profile)) == 0U)
                continue;
            aoahid_node* node = slot->device.node(static_cast<Profile>(profile));
            if (node == nullptr)
                continue;
            const aoahid_result result = aoahid_node_submit(node);
            if (result != AOAHID_OK) {
                failure = result;
                break;
            }
        }
        if (failure != AOAHID_OK)
            fail(*slot, failure);
    }

    for (const std::unique_ptr<Slot>& slot : slots_) {
        if (!slot->active.load(std::memory_order_relaxed))
            continue;
        aoahid_result failure = AOAHID_OK;
        bool sent = false;
        for (size_t profile = 0; profile < profile_count; ++profile) {
            if ((flushed & (uint32_t{1} << profile)) == 0U)
                continue;
            aoahid_node* node = slot->device.node(static_cast<Profile>(profile));
            if (node == nullptr)
                continue;
            // Drains the report queued above, including any continuation
            // fragment the profile still owes.
            const aoahid_result result = aoahid_node_submit_blocking(node, drain_deadline_ms);
            if (result != AOAHID_OK) {
                failure = result;
                break;
            }
            sent = true;
        }
        if (failure != AOAHID_OK) {
            fail(*slot, failure);
            continue;
        }
        // A device with none of the flushed profiles was sent nothing.
        if (sent)
            bump(slot->reports);
    }
    return true;
}

uint32_t open_profiles(Device& device, ProfileSetup& setup, std::string& error) {
    uint32_t opened = 0;
    for (size_t index = 0; index < profile_count; ++index) {
        const Profile profile = static_cast<Profile>(index);
        if (!profile_enabled(setup, profile) || device.has(profile))
            continue;
        std::string why;
        aoahid_spec* spec = nullptr;
        if (validate_profile(setup, profile, why) && build_spec(profile, setup, spec, why)) {
            const aoahid_result result = device.open_node(profile, spec);
            aoahid_spec_release(spec);
            if (result == AOAHID_OK) {
                opened |= profile_bit(profile);
                continue;
            }
            why = "Could not register the " + describe_profiles(profile_bit(profile)) + " on " +
                  device.label() + ". " + explain_error(result);
        }
        set_profile_enabled(setup, profile, false);
        if (!error.empty())
            error += ' ';
        error += why;
    }
    return opened;
}

uint32_t DeviceGroup::reconfigure(const size_t index, ProfileSetup wanted, std::string& error) {
    error.clear();
    if (index >= slots_.size() || !slots_[index]->active.load(std::memory_order_relaxed)) {
        error = "That device is not connected.";
        return 0U;
    }
    Slot& slot = *slots_[index];
    flush();
    if (!slot.active.load(std::memory_order_relaxed)) {
        error = "That device was dropped.";
        return 0U;
    }
    resolve_pen_surface(wanted);
    // This thread is the only writer of slot.setup, so it reads it unlocked.
    // Settings that cannot be registered are refused before anything is
    // closed, so a mistake never costs a HID that works.
    for (size_t profile = 0; profile < profile_count; ++profile) {
        const Profile kind = static_cast<Profile>(profile);
        if (profile_enabled(wanted, kind) &&
            !(slot.device.has(kind) && same_node(slot.setup, wanted, kind)) &&
            !validate_profile(wanted, kind, error))
            return 0U;
    }
    for (size_t profile = 0; profile < profile_count; ++profile) {
        const Profile kind = static_cast<Profile>(profile);
        if (!slot.device.has(kind) ||
            (profile_enabled(wanted, kind) && same_node(slot.setup, wanted, kind)))
            continue;
        slot.battery_sent[profile] = INT32_MIN;
        const aoahid_result closed = slot.device.close_node(kind);
        if (closed != AOAHID_OK) {
            // The Node is still registered in a state nobody knows.
            fail(slot, closed);
            error = slot.label + " was dropped: its " + describe_profiles(profile_bit(kind)) +
                    " could not be unregistered. " + explain_error(closed);
            return 0U;
        }
    }
    const uint32_t opened = open_profiles(slot.device, wanted, error);
    {
        const std::lock_guard lock(mutex_);
        slot.setup = wanted;
    }
    set_surfaces(slot);
    slot.profiles.store(slot.device.profile_mask(), std::memory_order_relaxed);
    refresh_mask();
    return opened;
}

void DeviceGroup::send_battery_levels(const size_t index) {
    flush();
    for (size_t at = 0; at < slots_.size(); ++at) {
        Slot& slot = *slots_[at];
        if ((index != SIZE_MAX && at != index) || !slot.active.load(std::memory_order_relaxed))
            continue;
        for (size_t profile = 0; profile < profile_count; ++profile) {
            const Profile kind = static_cast<Profile>(profile);
            aoahid_node* node = slot.device.node(kind);
            const BatterySetup& battery = battery_of(slot.setup, kind);
            if (node == nullptr || !battery.enabled || slot.battery_sent[profile] == battery.level)
                continue;
            aoahid_result result = aoahid_battery_update(node, battery.level);
            // Android may still be registering a HID that was just opened; a
            // refused report stays pending in the Node, so it is sent again.
            for (int attempt = 0; result == AOAHID_OK; ++attempt) {
                result = aoahid_node_submit_blocking(node, drain_deadline_ms);
                if (result != AOAHID_ERR_STALL || attempt == 19)
                    break;
                Timing::sleep_ms(10);
                result = AOAHID_OK;
            }
            if (result != AOAHID_OK) {
                fail(slot, result);
                break;
            }
            slot.battery_sent[profile] = battery.level;
            bump(slot.reports);
        }
    }
}

std::vector<DeviceStatus> DeviceGroup::snapshot() const {
    std::vector<DeviceStatus> list;
    const std::lock_guard lock(mutex_);
    list.reserve(slots_.size());
    for (const std::unique_ptr<Slot>& slot : slots_) {
        DeviceStatus status;
        status.label = slot->label;
        status.key = slot->key;
        status.skipped = slot->skipped.load(std::memory_order_relaxed);
        status.profiles = slot->profiles.load(std::memory_order_relaxed);
        status.setup = slot->setup;
        status.reports = slot->reports.load(std::memory_order_relaxed);
        status.errors = slot->errors.load(std::memory_order_relaxed);
        status.active = slot->active.load(std::memory_order_relaxed);
        status.last_error = slot->last_error;
        status.adb_port = slot->adb_port.load(std::memory_order_relaxed);
        list.push_back(std::move(status));
    }
    return list;
}

int DeviceGroup::start_adb_bridge(const size_t index, const uint16_t port) {
    if (index >= slots_.size() || !slots_[index]->active.load(std::memory_order_relaxed))
        return -1;
    Slot& slot = *slots_[index];
    const int result = slot.device.start_adb_bridge(port);
    slot.adb_port.store(slot.device.adb_port(), std::memory_order_relaxed);
    return result;
}

void DeviceGroup::stop_adb_bridge(const size_t index) noexcept {
    if (index >= slots_.size())
        return;
    Slot& slot = *slots_[index];
    slot.device.stop_adb_bridge();
    slot.adb_port.store(0U, std::memory_order_relaxed);
}

} // namespace aoap
