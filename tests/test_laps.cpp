// SPDX-License-Identifier: MIT
// Covers the lap model: LapSelector, lap_length(), next_lap(), LapWalk,
// lap_layout(), and state_at().
#include "doctest.h"

#include "aoahid_player/event_script.hpp"
#include "aoahid_player/input_state.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <ostream>
#include <string>
#include <vector>

using aoap::LapSelector;

namespace {

// The definition, written the slow way.
bool plain(const LapSelector& when, const uint64_t lap) {
    const bool match =
        lap >= when.first && lap <= when.last && (lap - when.first) % when.every == 0;
    return match != when.negate;
}

constexpr int64_t ms = 1'000'000;

aoap::EventScript load_text(const std::string& text) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("aoahid_player_test_laps_" + std::to_string(::rand()) + ".csv");
    {
        std::ofstream out(path, std::ios::binary);
        out << text;
    }
    aoap::EventScript script;
    std::string error;
    const bool loaded = script.load(path.string(), error);
    std::filesystem::remove(path);
    REQUIRE_MESSAGE(loaded, error);
    return script;
}

// Row 0 and 4 every lap; row 1 on lap 1; row 2 every third lap (5 ms lead,
// keep-time); row 3 on even laps, and 7 ms of waiting on the odd ones.
const char* const mixed = "k,A,1,10\n"
                          "@lap 1\n"
                          "k,B,1,20\n"
                          "@end\n"
                          "@every 3 keep-time\n"
                          "w,5\n"
                          "k,C,1,30\n"
                          "@end\n"
                          "@every 2\n"
                          "k,D,1,40\n"
                          "@else\n"
                          "w,7\n"
                          "@end\n"
                          "k,A,0,50\n";

struct Step {
    uint32_t row;
    int64_t due;
    bool operator==(const Step&) const = default;
};

std::vector<Step> walk(const aoap::EventScript& script, const uint64_t lap) {
    std::vector<Step> steps;
    aoap::LapWalk at;
    for (at.start(script, lap); !at.done; at.next(script))
        steps.push_back({at.row, at.due});
    CHECK(at.due == at.length);
    CHECK(at.row == script.rows.size());
    return steps;
}

bool same_state(const aoap::InputState& a, const aoap::InputState& b) {
    std::vector<aoap::EventPayload> releases;
    std::vector<aoap::EventPayload> presses;
    aoap::InputState::transition(a, b, releases, presses);
    return releases.empty() && presses.empty();
}

} // namespace

TEST_CASE("LapSelector functions agree with a lap-by-lap scan") {
    const uint64_t firsts[] = {1, 2, 5};
    const uint64_t spans[] = {0, 3, 15, UINT64_MAX};
    const uint64_t steps[] = {1, 2, 3, 7};
    for (const uint64_t first : firsts)
        for (const uint64_t span : spans)
            for (const uint64_t every : steps)
                for (const bool negate : {false, true}) {
                    const LapSelector when{first, span == UINT64_MAX ? span : first + span, every,
                                           negate, false};
                    for (uint64_t lap = 1; lap <= 40; ++lap) {
                        CAPTURE(first);
                        CAPTURE(when.last);
                        CAPTURE(every);
                        CAPTURE(negate);
                        CAPTURE(lap);
                        CHECK(aoap::runs_on(when, lap) == plain(when, lap));

                        uint64_t before = 0;
                        for (uint64_t m = lap - 1; m >= 1; --m) {
                            if (plain(when, m)) {
                                before = m;
                                break;
                            }
                        }
                        CHECK(aoap::last_run_before(when, lap) == before);

                        // Every selector here either runs again within 60
                        // laps or never does.
                        uint64_t next = 0;
                        for (uint64_t m = lap; m <= lap + 60; ++m) {
                            if (plain(when, m)) {
                                next = m;
                                break;
                            }
                        }
                        CHECK(aoap::next_run_from(when, lap) == next);
                    }
                }
}

TEST_CASE("LapSelector handles laps near the top of the range") {
    const uint64_t top = UINT64_MAX; // divisible by 3
    const LapSelector thirds{3, top, 3, false, false};
    CHECK(aoap::runs_on(thirds, top));
    CHECK(aoap::next_run_from(thirds, top - 1) == top);
    CHECK(aoap::next_run_from(thirds, top) == top);
    CHECK(aoap::last_run_before(thirds, top) == top - 3);

    // From top - 1 the next match would be top + 1: there is none.
    const LapSelector sevens{2, top, 7, false, false};
    CHECK_FALSE(aoap::runs_on(sevens, top - 1));
    CHECK_FALSE(aoap::runs_on(sevens, top));
    CHECK(aoap::next_run_from(sevens, top - 1) == 0);

    const LapSelector never{1, top, 1, true, false}; // @else of "every lap"
    CHECK(aoap::next_run_from(never, 1) == 0);
    CHECK(aoap::last_run_before(never, 1000) == 0);

    const LapSelector not_thirds{3, top, 3, true, false};
    CHECK(aoap::next_run_from(not_thirds, top) == 0);
    CHECK(aoap::last_run_before(not_thirds, 1) == 0);
    CHECK(aoap::last_run_before(not_thirds, 4) == 2);
}

TEST_CASE("lap_length adds what runs and what keeps its time") {
    const aoap::EventScript script = load_text(mixed);
    CHECK(aoap::lap_length(script, 1) == 122 * ms); // 10 + 20 + 35 kept + 7 + 50
    CHECK(aoap::lap_length(script, 2) == 135 * ms); // 10 + 35 kept + 40 + 50
    CHECK(aoap::lap_length(script, 3) == 102 * ms); // 10 + 35 + 7 + 50
    CHECK(aoap::lap_length(script, 6) == 135 * ms); // 10 + 35 + 40 + 50
}

TEST_CASE("LapWalk visits the rows of a lap at their start times") {
    const aoap::EventScript script = load_text(mixed);
    CHECK(walk(script, 1) == std::vector<Step>{{0, 0}, {1, 10 * ms}, {4, 72 * ms}});
    CHECK(walk(script, 2) == std::vector<Step>{{0, 0}, {3, 45 * ms}, {4, 85 * ms}});
    CHECK(walk(script, 3) == std::vector<Step>{{0, 0}, {2, 15 * ms}, {4, 52 * ms}});
    CHECK(walk(script, 6) ==
          std::vector<Step>{{0, 0}, {2, 15 * ms}, {3, 45 * ms}, {4, 85 * ms}});
}

TEST_CASE("LapWalk::seek lands on the first row at or after a time") {
    const aoap::EventScript script = load_text(mixed);
    aoap::LapWalk at;
    at.seek(script, 2, 0);
    CHECK(at.row == 0);
    at.seek(script, 2, 45 * ms);
    CHECK(at.row == 3);
    CHECK(at.due == 45 * ms);
    at.seek(script, 2, 45 * ms + 1);
    CHECK(at.row == 4);
    CHECK(at.due == 85 * ms);
    at.seek(script, 2, 999 * ms);
    CHECK(at.done);
    CHECK(at.due == 135 * ms);
    CHECK(at.lap == 2);
}

TEST_CASE("A lap with nothing on it is done at once") {
    const aoap::EventScript script = load_text("@every 5\nk,A,1,10\n@end\n");
    aoap::LapWalk at;
    at.start(script, 1);
    CHECK(at.done);
    CHECK(at.length == 0);
    CHECK(at.due == 0);
    at.start(script, 5);
    CHECK_FALSE(at.done);
    CHECK(at.length == 10 * ms);
}

TEST_CASE("next_lap finds the next lap with rows, or with anything") {
    const aoap::EventScript bounded = load_text("@lap 2..3\nk,A,1,10\n@end\n");
    CHECK(aoap::next_lap(bounded, 1, true) == 2);
    CHECK(aoap::next_lap(bounded, 3, true) == 3);
    CHECK(aoap::next_lap(bounded, 4, true) == 0);
    CHECK(aoap::next_lap(bounded, 4, false) == 0);

    const aoap::EventScript sparse = load_text("@every 5\nk,A,1,10\n@end\n");
    CHECK(aoap::next_lap(sparse, 1, false) == 5);
    CHECK(aoap::next_lap(sparse, 6, false) == 10);

    // The other laps only wait: they have no rows but still take time.
    const aoap::EventScript waits = load_text("@every 4\nk,A,1,10\n@else\nw,5\n@end\n");
    CHECK(aoap::next_lap(waits, 1, true) == 4);
    CHECK(aoap::next_lap(waits, 1, false) == 1);
    CHECK(aoap::next_lap(waits, 4, false) == 4);

    const aoap::EventScript kept = load_text("@every 4 keep-time\nk,A,1,10\n@end\n");
    CHECK(aoap::next_lap(kept, 1, true) == 4);
    CHECK(aoap::next_lap(kept, 1, false) == 1);

    // A far lap is found without stepping through the ones before it.
    const aoap::EventScript far = load_text("@lap 1000000000000\nk,A,1,10\n@end\n");
    CHECK(aoap::next_lap(far, 1, false) == 1000000000000ULL);
    CHECK(aoap::next_lap(far, 1000000000001ULL, true) == 0);

    const aoap::EventScript plain_rows = load_text("k,A,1,10\n");
    CHECK(aoap::next_lap(plain_rows, 7, true) == 7);
}

TEST_CASE("lap_layout gives the parts of a lap that run") {
    const aoap::EventScript script = load_text(mixed);
    std::vector<aoap::LapSpan> spans;
    CHECK(aoap::lap_layout(script, 1, spans) == 122 * ms);
    REQUIRE(spans.size() == 4);
    CHECK(spans[0].start == 0);
    CHECK(spans[0].end == 10 * ms);
    CHECK_FALSE(spans[0].conditional);
    CHECK(spans[1].start == 10 * ms);
    CHECK(spans[1].end == 30 * ms);
    CHECK(spans[1].conditional);
    // 30..65 ms is the kept time of the block that does not run on lap 1.
    CHECK(spans[2].start == 65 * ms);
    CHECK(spans[2].end == 72 * ms);
    CHECK(spans[2].conditional);
    CHECK(spans[3].start == 72 * ms);
    CHECK(spans[3].end == 122 * ms);
    CHECK_FALSE(spans[3].conditional);

    // Neighbouring parts of one kind join; the vector is reused.
    CHECK(aoap::lap_layout(script, 6, spans) == 135 * ms);
    REQUIRE(spans.size() == 3);
    CHECK(spans[1].start == 10 * ms);
    CHECK(spans[1].end == 85 * ms);
    CHECK(spans[1].conditional);
}

TEST_CASE("state_at matches playing every lap from the first") {
    const aoap::EventScript script = load_text("k,A,1,1\n"
                                               "@lap 1\n"
                                               "k,B,1,1\n"
                                               "@end\n"
                                               "@every 3\n"
                                               "k,B,0,1\n"
                                               "k,C,1,1\n"
                                               "@else\n"
                                               "k,C,0,1\n"
                                               "@end\n"
                                               "@every 4 from 2 to 14\n"
                                               "t,0,1,5,5,1\n"
                                               "@end\n"
                                               "@lap 9..\n"
                                               "t,0,0,5,5,1\n"
                                               "k,A,0,1\n"
                                               "@end\n"
                                               "c,VolumeUp,1,1\n");
    std::vector<uint32_t> order;
    aoap::InputState played; // the state at the start of `lap`, kept across the loop
    for (uint64_t lap = 1; lap <= 40; ++lap) {
        aoap::InputState expected = played;
        aoap::LapWalk at;
        at.start(script, lap);
        while (true) {
            const size_t row = at.done ? script.rows.size() : at.row;
            aoap::InputState state;
            aoap::state_at(state, script, lap, row, order);
            CAPTURE(lap);
            CAPTURE(row);
            CHECK(same_state(state, expected));
            if (at.done)
                break;
            expected.apply(script.rows[at.row].payload);
            at.next(script);
        }
        played = expected;
    }
}

TEST_CASE("state_at on the first row of lap 1 is neutral") {
    const aoap::EventScript script = load_text("k,A,1,1\n");
    std::vector<uint32_t> order;
    aoap::InputState state;
    state.apply(aoap::KeyEvent{0x05, true});
    aoap::state_at(state, script, 1, 0, order);
    CHECK(state.neutral());
}
