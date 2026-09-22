// SPDX-License-Identifier: MIT
// Covers InputState, which the player uses to release everything on stop or
// pause and to rebuild the held state at a seek target.
#include "doctest.h"

#include "aoahid_player/input_state.hpp"

#include <ostream>
#include <string>
#include <vector>

using aoap::EventPayload;
using aoap::InputState;

namespace {

template <typename T>
size_t count_of(const std::vector<EventPayload>& rows) {
    size_t count = 0;
    for (const EventPayload& row : rows)
        count += std::holds_alternative<T>(row) ? 1U : 0U;
    return count;
}

} // namespace

TEST_CASE("A fresh InputState is neutral and mouse motion leaves no state") {
    InputState state;
    CHECK(state.neutral());
    state.apply(aoap::MouseMove{10, -3});
    CHECK(state.neutral());
}

TEST_CASE("Releasing everything lifts contacts at their last position") {
    InputState held;
    held.apply(aoap::TouchEvent{2, true, 100, 200});
    held.apply(aoap::TouchEvent{2, true, 150, 250}); // moved
    held.apply(aoap::KeyEvent{0x04, true});
    held.apply(aoap::MouseButton{1, true});
    held.apply(aoap::GamepadButton{3, true});
    held.apply(aoap::GamepadAxis{1, -500});
    held.apply(aoap::GamepadDpad{true, false, false, false});
    held.apply(aoap::PenSample{true, true, 5, 6, 100});
    held.apply(aoap::ConsumerEvent{0xE9, true}); // Volume Up
    CHECK_FALSE(held.neutral());

    std::vector<EventPayload> releases;
    std::vector<EventPayload> presses;
    InputState::transition(held, InputState{}, releases, presses);
    CHECK(presses.empty());
    CHECK(releases.size() == 8);

    const auto* lift = std::get_if<aoap::TouchEvent>(&releases.front());
    REQUIRE(lift != nullptr);
    CHECK(lift->finger_id == 2);
    CHECK_FALSE(lift->state);
    CHECK(lift->x == 150);
    CHECK(lift->y == 250);
    CHECK(count_of<aoap::KeyEvent>(releases) == 1);
    CHECK(count_of<aoap::PenSample>(releases) == 1);
    CHECK(count_of<aoap::ConsumerEvent>(releases) == 1);
}

TEST_CASE("A Consumer usage and a Keyboard usage with the same number track separately") {
    InputState held;
    held.apply(aoap::KeyEvent{0xE2, true});      // some keyboard usage
    held.apply(aoap::ConsumerEvent{0xE2, true}); // Mute — same numeric value
    CHECK_FALSE(held.neutral());

    std::vector<EventPayload> releases;
    std::vector<EventPayload> presses;
    InputState::transition(held, InputState{}, releases, presses);
    CHECK(releases.size() == 2);
    CHECK(count_of<aoap::KeyEvent>(releases) == 1);
    CHECK(count_of<aoap::ConsumerEvent>(releases) == 1);
}

TEST_CASE("A transition only sends what differs") {
    InputState from;
    from.apply(aoap::TouchEvent{0, true, 10, 10});
    from.apply(aoap::KeyEvent{0x05, true});

    InputState to;
    to.apply(aoap::TouchEvent{0, true, 10, 10}); // unchanged
    to.apply(aoap::TouchEvent{1, true, 20, 20}); // new
    to.apply(aoap::KeyEvent{0x06, true});        // 0x05 up, 0x06 down

    std::vector<EventPayload> releases;
    std::vector<EventPayload> presses;
    InputState::transition(from, to, releases, presses);
    CHECK(releases.size() == 1);
    CHECK(count_of<aoap::KeyEvent>(releases) == 1);
    CHECK(presses.size() == 2);
    CHECK(count_of<aoap::TouchEvent>(presses) == 1);
    CHECK(count_of<aoap::KeyEvent>(presses) == 1);

    InputState::transition(to, to, releases, presses);
    CHECK(releases.empty());
    CHECK(presses.empty());
}

TEST_CASE("Out-of-range controls are ignored rather than tracked") {
    InputState state;
    state.apply(aoap::TouchEvent{16, true, 1, 1});
    state.apply(aoap::KeyEvent{0x1FF, true});
    state.apply(aoap::GamepadAxis{InputState::max_axes, 5});
    state.apply(aoap::ConsumerEvent{0x1FF, true});
    CHECK(state.neutral());
}

namespace {

bool same_state(const InputState& a, const InputState& b) {
    std::vector<EventPayload> releases;
    std::vector<EventPayload> presses;
    InputState::transition(a, b, releases, presses);
    return releases.empty() && presses.empty();
}

} // namespace

TEST_CASE("state_at rebuilds the state at a row of either lap") {
    aoap::EventScript script;
    script.rows.push_back({aoap::GamepadAxis{0, 100}, false, 1'000'000}); // repeated
    script.rows.push_back({aoap::GamepadAxis{0, 5}, true, 1'000'000});    // once, writes after it
    script.rows.push_back({aoap::GamepadAxis{1, 7}, false, 1'000'000});   // repeated
    const aoap::Timeline timeline = aoap::build_timeline(script);

    InputState state;
    aoap::state_at(state, script, timeline, aoap::Lap::first, 0, 0);
    CHECK(state.neutral());

    // Rows 0 and 1 ran; the once row wrote axis 0 last.
    InputState expected;
    expected.apply(aoap::GamepadAxis{0, 5});
    aoap::state_at(state, script, timeline, aoap::Lap::first, 2, 0);
    CHECK(same_state(state, expected));

    // Lap 1 is done: every row ran once.
    expected.apply(aoap::GamepadAxis{1, 7});
    aoap::state_at(state, script, timeline, aoap::Lap::repeat, 0, 1);
    CHECK(same_state(state, expected));

    // A repeat lap has run since: its write to axis 0 is the last one.
    InputState after_repeat;
    after_repeat.apply(aoap::GamepadAxis{0, 100});
    after_repeat.apply(aoap::GamepadAxis{1, 7});
    aoap::state_at(state, script, timeline, aoap::Lap::repeat, 0, 2);
    CHECK(same_state(state, after_repeat));

    // Inside the repeat lap, after its first row.
    aoap::state_at(state, script, timeline, aoap::Lap::repeat, 1, 1);
    CHECK(same_state(state, after_repeat));
}

TEST_CASE("state_at without once rows treats the lap as one loop") {
    aoap::EventScript script;
    script.rows.push_back({aoap::GamepadAxis{0, 9}, false, 1'000'000});
    script.rows.push_back({aoap::GamepadAxis{0, 0}, false, 1'000'000});
    const aoap::Timeline timeline = aoap::build_timeline(script);

    InputState state;
    aoap::state_at(state, script, timeline, aoap::Lap::first, 1, 0);
    InputState nine;
    nine.apply(aoap::GamepadAxis{0, 9});
    CHECK(same_state(state, nine));

    aoap::state_at(state, script, timeline, aoap::Lap::repeat, 0, 1);
    CHECK(state.neutral());
}

TEST_CASE("lap_warnings names what a repeat lap leaves changed") {
    aoap::EventScript script;
    script.rows.push_back({aoap::TouchEvent{0, true, 5, 5}, false, 1'000'000});  // repeated: down
    script.rows.push_back({aoap::TouchEvent{0, false, 5, 5}, true, 1'000'000});  // once: up
    const aoap::Timeline timeline = aoap::build_timeline(script);

    // Lap 1 ends released, lap 2 ends pressed: lap 3 would start pressed.
    const std::vector<std::string> warnings = aoap::lap_warnings(script, timeline);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("finger 0") != std::string::npos);
}

TEST_CASE("lap_warnings is quiet for balanced scripts") {
    aoap::EventScript balanced;
    balanced.rows.push_back({aoap::TouchEvent{0, true, 5, 5}, false, 1'000'000});
    balanced.rows.push_back({aoap::TouchEvent{0, false, 5, 5}, false, 1'000'000});
    balanced.rows.push_back({aoap::KeyEvent{0x04, true}, true, 1'000'000}); // once, left held
    CHECK(aoap::lap_warnings(balanced, aoap::build_timeline(balanced)).empty());

    aoap::EventScript no_once;
    no_once.rows.push_back({aoap::TouchEvent{0, true, 5, 5}, false, 1'000'000});
    CHECK(aoap::lap_warnings(no_once, aoap::build_timeline(no_once)).empty());

    aoap::EventScript once_only;
    once_only.rows.push_back({aoap::TouchEvent{0, true, 5, 5}, true, 1'000'000});
    CHECK(aoap::lap_warnings(once_only, aoap::build_timeline(once_only)).empty());
}
