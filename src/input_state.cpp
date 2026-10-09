// SPDX-License-Identifier: MIT
#include "aoahid_player/input_state.hpp"

#include <algorithm>
#include <type_traits>
#include <variant>

namespace aoap {
namespace {

void set_member(std::vector<uint32_t>& set, const uint32_t value, const bool present) {
    const auto it = std::lower_bound(set.begin(), set.end(), value);
    const bool found = it != set.end() && *it == value;
    if (present && !found)
        set.insert(it, value);
    else if (!present && found)
        set.erase(it);
}

// Emits one row per element that is in `a` but not in `b`.
template <typename Row>
void difference(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, const bool pressed,
                std::vector<EventPayload>& out) {
    for (const uint32_t value : a) {
        if (!std::binary_search(b.begin(), b.end(), value))
            out.push_back(Row{value, pressed});
    }
}

bool same_dpad(const GamepadDpad& a, const GamepadDpad& b) noexcept {
    return a.up == b.up && a.down == b.down && a.right == b.right && a.left == b.left;
}

bool dpad_neutral(const GamepadDpad& d) noexcept { return !d.up && !d.down && !d.right && !d.left; }

bool same_pen(const PenSample& a, const PenSample& b) noexcept {
    if (a.in_range != b.in_range)
        return false;
    if (!a.in_range)
        return true;
    return a.tip == b.tip && a.x == b.x && a.y == b.y && a.pressure == b.pressure;
}

} // namespace

InputState::InputState() {
    // Reserved so tracking a press during playback never allocates.
    mouse_buttons_.reserve(16);
    pad_buttons_.reserve(32);
}

void InputState::apply(const EventPayload& payload) {
    std::visit(
        [this](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, TouchEvent>) {
                if (value.finger_id < 0 || static_cast<size_t>(value.finger_id) >= max_contacts)
                    return;
                contacts_[static_cast<size_t>(value.finger_id)] = {value.state, value.x, value.y};
            } else if constexpr (std::is_same_v<T, MouseButton>) {
                set_member(mouse_buttons_, value.button, value.pressed);
            } else if constexpr (std::is_same_v<T, KeyEvent>) {
                if (value.usage < keys_.size())
                    keys_.set(value.usage, value.down);
            } else if constexpr (std::is_same_v<T, GamepadButton>) {
                set_member(pad_buttons_, value.button, value.pressed);
            } else if constexpr (std::is_same_v<T, GamepadAxis>) {
                if (value.axis_index < max_axes)
                    axes_[value.axis_index] = value.value;
            } else if constexpr (std::is_same_v<T, GamepadDpad>) {
                dpad_ = value;
            } else if constexpr (std::is_same_v<T, PenSample>) {
                pen_ = value;
            } else if constexpr (std::is_same_v<T, MediaKey>) {
                // The toggle profile has one Consumer field: a press replaces
                // whatever was held, and any release clears it (see mutate()
                // in device_group.cpp).
                media_ = value.down ? value.usage : uint16_t{0};
            }
            // MouseMove is relative and leaves no state behind.
        },
        payload);
}

void InputState::clear() noexcept {
    contacts_.fill({});
    keys_.reset();
    mouse_buttons_.clear();
    pad_buttons_.clear();
    axes_.fill(0);
    dpad_ = {};
    pen_ = {};
    media_ = 0U;
}

bool InputState::neutral() const noexcept {
    for (const Contact& contact : contacts_) {
        if (contact.active)
            return false;
    }
    for (const int32_t axis : axes_) {
        if (axis != 0)
            return false;
    }
    return keys_.none() && mouse_buttons_.empty() && pad_buttons_.empty() && dpad_neutral(dpad_) &&
           !pen_.in_range && media_ == 0U;
}

void InputState::transition(const InputState& from, const InputState& to,
                            std::vector<EventPayload>& releases,
                            std::vector<EventPayload>& presses) {
    releases.clear();
    presses.clear();

    for (size_t id = 0; id < max_contacts; ++id) {
        const Contact& a = from.contacts_[id];
        const Contact& b = to.contacts_[id];
        const int finger = static_cast<int>(id);
        if (a.active && !b.active)
            releases.push_back(TouchEvent{finger, false, a.x, a.y});
        else if (b.active && (!a.active || a.x != b.x || a.y != b.y))
            presses.push_back(TouchEvent{finger, true, b.x, b.y});
    }

    for (size_t usage = 0; usage < from.keys_.size(); ++usage) {
        const bool a = from.keys_.test(usage);
        const bool b = to.keys_.test(usage);
        if (a != b)
            (b ? presses : releases).push_back(KeyEvent{static_cast<uint16_t>(usage), b});
    }

    difference<MouseButton>(from.mouse_buttons_, to.mouse_buttons_, false, releases);
    difference<MouseButton>(to.mouse_buttons_, from.mouse_buttons_, true, presses);
    difference<GamepadButton>(from.pad_buttons_, to.pad_buttons_, false, releases);
    difference<GamepadButton>(to.pad_buttons_, from.pad_buttons_, true, presses);

    for (size_t index = 0; index < max_axes; ++index) {
        const int32_t target = to.axes_[index];
        if (from.axes_[index] != target)
            (target == 0 ? releases : presses).push_back(GamepadAxis{index, target});
    }

    if (!same_dpad(from.dpad_, to.dpad_))
        (dpad_neutral(to.dpad_) ? releases : presses).push_back(to.dpad_);

    if (from.media_ != to.media_) {
        if (from.media_ != 0U)
            releases.push_back(MediaKey{from.media_, false});
        if (to.media_ != 0U)
            presses.push_back(MediaKey{to.media_, true});
    }

    if (!same_pen(from.pen_, to.pen_)) {
        if (to.pen_.in_range)
            presses.push_back(to.pen_);
        else
            releases.push_back(PenSample{false, false, from.pen_.x, from.pen_.y, 0});
    }
}

void state_at(InputState& out, const EventScript& script, const uint64_t lap, const size_t row,
              std::vector<uint32_t>& order) {
    out.clear();
    const std::vector<Segment>& segments = script.segments;

    // What the earlier laps left: each segment's last run, oldest first.
    order.clear();
    for (uint32_t index = 0; index < segments.size(); ++index) {
        if (last_run_before(segments[index].when, lap) != 0)
            order.push_back(index);
    }
    std::sort(order.begin(), order.end(), [&](const uint32_t a, const uint32_t b) {
        const uint64_t lap_a = last_run_before(segments[a].when, lap);
        const uint64_t lap_b = last_run_before(segments[b].when, lap);
        return lap_a != lap_b ? lap_a < lap_b : a < b;
    });
    for (const uint32_t index : order) {
        for (uint32_t at = segments[index].begin; at < segments[index].end; ++at)
            out.apply(script.rows[at].payload);
    }

    // Then this lap, up to `row`.
    for (const Segment& segment : segments) {
        if (!runs_on(segment.when, lap))
            continue;
        const size_t end = std::min<size_t>(segment.end, row);
        for (size_t at = segment.begin; at < end; ++at)
            out.apply(script.rows[at].payload);
    }
}

} // namespace aoap
