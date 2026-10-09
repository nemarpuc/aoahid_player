// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

// This header is deliberately free of <aoahid.h>: aoa_record shares the CSV
// row format with aoa_touch but never links libaoahid.

namespace aoap {

// One parsed CSV line. See README.md's CSV format table for the exact column
// layout per prefix.
struct TouchEvent   { int finger_id; bool state; int32_t x, y; };
struct MouseMove    { int32_t dx, dy; };
struct MouseButton  { uint32_t button; bool pressed; };
struct KeyEvent     { uint16_t usage; bool down; };
struct GamepadButton{ uint32_t button; bool pressed; };
struct GamepadAxis  { size_t axis_index; int32_t value; };
struct GamepadDpad  { bool up, down, right, left; };
struct PenSample    { bool in_range, tip; int32_t x, y, pressure; };
struct MediaKey     { uint16_t usage; bool down; }; // Consumer page, toggle profile

using EventPayload = std::variant<TouchEvent, MouseMove, MouseButton, KeyEvent,
                                   GamepadButton, GamepadAxis, GamepadDpad, PenSample,
                                   MediaKey>;

struct EventRecord {
    EventPayload payload;
    int64_t wait_ns; // 0 = batch with next row, no flush/wait
};

// Identifies what a row touches, so the player can tell a batchable row (two
// fingers in one frame) from one that must flush first (press then release of
// the same control, which libaoahid rejects as an unreported opposite edge).
// Zero means "accumulates, never conflicts".
uint64_t batch_key(const EventPayload& payload) noexcept;

// Which laps a block of rows runs on. Laps count from 1. A lap matches when
// first <= lap <= last and (lap - first) % every == 0; `negate` (an @else
// block) runs on the laps that do not match.
struct LapSelector {
    uint64_t first{1};
    uint64_t last{UINT64_MAX};
    uint64_t every{1};
    bool negate{};
    bool keep_time{}; // laps it does not run on still wait its duration
};

[[nodiscard]] bool runs_on(const LapSelector& when, uint64_t lap) noexcept;
// Latest lap before `lap` the selector runs on; 0 when there is none.
[[nodiscard]] uint64_t last_run_before(const LapSelector& when, uint64_t lap) noexcept;
// Earliest lap from `lap` on the selector runs on; 0 when there is none.
[[nodiscard]] uint64_t next_run_from(const LapSelector& when, uint64_t lap) noexcept;

// Consecutive rows that share one selector. A script's segments cover its
// rows in file order.
struct Segment {
    uint32_t begin{};
    uint32_t end{};     // rows[begin, end)
    LapSelector when;
    int64_t lead_ns{};  // wait before the first row: `w` rows written first
    int64_t duration{}; // lead_ns plus every row's wait_ns
};

// Parses a single CSV file into rows kept in the order they are written.
class EventScript {
  public:
    std::vector<EventRecord> rows;

    // Rows grouped by the laps they run on; a part that only waits has no
    // rows. Filled by load().
    std::vector<Segment> segments;
    uint32_t lap_blocks{}; // @lap and @every blocks in the file

    // Every problem load() found, "<file>:<line>: <reason>", up to 50; a
    // failed load leaves `rows` incomplete and must not be played.
    std::vector<std::string> errors;

    // The coordinate space the touch and pen rows were written in, from
    // "@screen WxH" (aoa_record writes one); 0 when the file does not say,
    // in which case coordinates are used as they are.
    int32_t screen_width{};
    int32_t screen_height{};

    // True when the file said "@coords normalized": touch and pen x,y were
    // written as fractions 0..1 and load() turned them into integers in a
    // 65536 x 65536 space (which is then screen_width x screen_height).
    bool coords_normalized{};

    // `path` is UTF-8. Returns false and fills `error` with the first problem
    // ("<file>:<line>: <reason>", plus how many more `errors` holds). A '#'
    // comment, a blank line, and a UTF-8 BOM are skipped; nothing else is
    // dropped. Lines starting with '@' are directives: @format, @screen,
    // @coords, and the lap blocks @lap / @every [... @else] ... @end.
    bool load(const std::string& path, std::string& error);

    [[nodiscard]] size_t size() const noexcept { return rows.size(); }

    // Union of profile_bit() values the script needs; declared as a plain mask
    // so this header stays independent of device.hpp.
    [[nodiscard]] uint32_t required_profiles() const noexcept;
};

// Length of `lap`: the waits of everything that runs on it, plus the
// duration of the keep-time blocks that do not.
[[nodiscard]] int64_t lap_length(const EventScript& script, uint64_t lap) noexcept;

// Earliest lap from `from` on with something on it: a row when `rows_only`,
// otherwise a row or a wait. 0 when there is none.
[[nodiscard]] uint64_t next_lap(const EventScript& script, uint64_t from,
                                bool rows_only) noexcept;

// Steps through the rows that run on one lap, in file order. Each row's
// start is measured from the start of the lap.
struct LapWalk {
    uint64_t lap{1};
    int64_t length{}; // of the whole lap
    int64_t due{};    // start of `row`; `length` once done
    uint32_t row{};   // index into script.rows; rows.size() once done
    bool done{true};

    void start(const EventScript& script, uint64_t lap_number) noexcept;
    // Stops on the first row that starts at or after `time_ns`.
    void seek(const EventScript& script, uint64_t lap_number, int64_t time_ns) noexcept;
    void next(const EventScript& script) noexcept;

  private:
    void settle(const EventScript& script) noexcept;

    uint32_t segment_{};
    int64_t time_{};
};

// One stretch of a lap that runs; `conditional` when it comes from a lap
// block rather than from rows that run every lap.
struct LapSpan {
    int64_t start;
    int64_t end;
    bool conditional;
};

// The stretches of `lap` that run, in order, for drawing it. Time a
// keep-time block holds while it does not run is left as a gap. Returns the
// lap's length.
int64_t lap_layout(const EventScript& script, uint64_t lap, std::vector<LapSpan>& out);

// Maps a script written for its screen_width x screen_height space onto the
// connected surfaces: touch rows onto touch_width x touch_height, pen rows
// onto pen_width x pen_height (0 leaves that profile alone). Returns false,
// leaving `out` untouched, when the script declares no space or every size
// already matches. Scaling happens once here, never per row while playing.
bool scale_script(const EventScript& script, int32_t touch_width, int32_t touch_height,
                  int32_t pen_width, int32_t pen_height, EventScript& out);

// Side of the square space "@coords normalized" positions are stored in.
inline constexpr int32_t normalized_space = 65536;

// --- Row formatting, shared with the recorder ----------------------------

inline void append_touch_row(std::string& out, const int finger_id, const bool state,
                             const int32_t x, const int32_t y, const double wait_ms) {
    char line[96];
    const int written = std::snprintf(line, sizeof line, "t,%d,%d,%d,%d,%.3f\n", finger_id,
                                      state ? 1 : 0, x, y, wait_ms);
    if (written > 0 && static_cast<size_t>(written) < sizeof line)
        out.append(line, static_cast<size_t>(written));
}

// A touch row with positions as fractions 0..1, for "@coords normalized".
inline void append_touch_row_fraction(std::string& out, const int finger_id, const bool state,
                                      const double x, const double y, const double wait_ms) {
    char line[112];
    const int written = std::snprintf(line, sizeof line, "t,%d,%d,%.6f,%.6f,%.3f\n", finger_id,
                                      state ? 1 : 0, x, y, wait_ms);
    if (written > 0 && static_cast<size_t>(written) < sizeof line)
        out.append(line, static_cast<size_t>(written));
}

inline void append_key_row(std::string& out, const uint16_t usage, const bool down,
                           const double wait_ms) {
    char line[64];
    const int written = std::snprintf(line, sizeof line, "k,0x%02x,%d,%.3f\n",
                                      static_cast<unsigned>(usage), down ? 1 : 0, wait_ms);
    if (written > 0 && static_cast<size_t>(written) < sizeof line)
        out.append(line, static_cast<size_t>(written));
}

} // namespace aoap
