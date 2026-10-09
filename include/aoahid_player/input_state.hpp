// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "event_script.hpp"

namespace aoap {

// The absolute input state a sequence of script rows leaves behind: active
// contacts, held keys and buttons, axis values, the D-pad, the pen, and the
// media key held down.
//
// libaoahid has no release-all call and rejects lifting a contact that is not
// down, so the player keeps one of these for what it has actually sent. On
// stop or pause it transitions to the neutral state; on a seek it rebuilds
// the state at the target and transitions to that, so the device ends up
// exactly where uninterrupted playback would have left it.
//
// Relative mouse motion has no absolute state and is not tracked.
class InputState {
  public:
    static constexpr size_t max_contacts = 16;
    static constexpr size_t max_axes = 64;

    InputState();

    void apply(const EventPayload& payload);
    void clear() noexcept;
    [[nodiscard]] bool neutral() const noexcept;

    // Rows that take a device from `from` to `to`. Releases are returned
    // separately so they can be sent in their own report first; a lifted
    // contact then frees its slot before another one is placed.
    static void transition(const InputState& from, const InputState& to,
                           std::vector<EventPayload>& releases,
                           std::vector<EventPayload>& presses);

  private:
    struct Contact {
        bool active{};
        int32_t x{};
        int32_t y{};
    };

    std::array<Contact, max_contacts> contacts_{};
    std::bitset<256> keys_{};
    std::vector<uint32_t> mouse_buttons_; // sorted
    std::vector<uint32_t> pad_buttons_;   // sorted
    std::array<int32_t, max_axes> axes_{};
    GamepadDpad dpad_{};
    PenSample pen_{};
    uint16_t media_{}; // the Consumer usage held down, 0 for none
};

// The state uninterrupted playback holds just before `row` of `lap`
// (script.rows.size() for the end of the lap). Every row sets an absolute
// value, so the latest earlier lap each segment ran on stands for all of its
// runs; the cost does not grow with the lap number. `order` is scratch:
// reserve script.segments.size() to keep the call free of allocation.
void state_at(InputState& out, const EventScript& script, uint64_t lap, size_t row,
              std::vector<uint32_t>& order);

} // namespace aoap
