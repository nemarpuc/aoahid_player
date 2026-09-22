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

// One parsed CSV line. `once` is true for an uppercase prefix (runs only on
// the first lap); false for lowercase (runs on every lap).
// See README.md's CSV format table for the exact column layout per prefix.
struct TouchEvent   { int finger_id; bool state; int32_t x, y; };
struct MouseMove    { int32_t dx, dy; };
struct MouseButton  { uint32_t button; bool pressed; };
struct KeyEvent     { uint16_t usage; bool down; };
struct GamepadButton{ uint32_t button; bool pressed; };
struct GamepadAxis  { size_t axis_index; int32_t value; };
struct GamepadDpad  { bool up, down, right, left; };
struct PenSample    { bool in_range, tip; int32_t x, y, pressure; };
// A Consumer-page media key (volume, mute, play/pause, next/previous track,
// stop). Live/API only: no CSV row prefix exists for this, so it never
// appears in a loaded or recorded EventScript (see Profile::consumer).
struct ConsumerEvent{ uint16_t usage; bool down; };

using EventPayload = std::variant<TouchEvent, MouseMove, MouseButton, KeyEvent,
                                   GamepadButton, GamepadAxis, GamepadDpad, PenSample,
                                   ConsumerEvent>;

struct EventRecord {
    EventPayload payload;
    bool once;          // true = uppercase prefix or inside @once, first lap only
    int64_t wait_ns;    // 0 = batch with next row, no flush/wait
    bool keep_time{};   // once rows only (@once keep-time): later laps skip the row
                        // but still wait its wait_ns
};

// Identifies what a row touches, so the player can tell a batchable row (two
// fingers in one frame) from one that must flush first (press then release of
// the same control, which libaoahid rejects as an unreported opposite edge).
// Zero means "accumulates, never conflicts".
uint64_t batch_key(const EventPayload& payload) noexcept;

// Parses a single CSV file into rows kept in the order they are written.
class EventScript {
  public:
    std::vector<EventRecord> rows;

    // Every problem load() found, "<file>:<line>: <reason>", up to 50; a
    // failed load leaves `rows` incomplete and must not be played.
    std::vector<std::string> errors;

    // The coordinate space the touch and pen rows were written in, from a
    // "# screen WxH" comment line (aoa_record writes one); 0 when the file
    // does not say, in which case coordinates are used as they are.
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
    // @coords, and @once [keep-time] ... @end.
    bool load(const std::string& path, std::string& error);

    [[nodiscard]] size_t size() const noexcept { return rows.size(); }

    // Union of profile_bit() values the script needs; declared as a plain mask
    // so this header stays independent of device.hpp.
    [[nodiscard]] uint32_t required_profiles() const noexcept;
};

// Maps a script written for its screen_width x screen_height space onto the
// connected surfaces: touch rows onto touch_width x touch_height, pen rows
// onto pen_width x pen_height (0 leaves that profile alone). Returns false,
// leaving `out` untouched, when the script declares no space or every size
// already matches. Scaling happens once here, never per row while playing.
bool scale_script(const EventScript& script, int32_t touch_width, int32_t touch_height,
                  int32_t pen_width, int32_t pen_height, EventScript& out);

// Side of the square space "@coords normalized" positions are stored in.
inline constexpr int32_t normalized_space = 65536;

// The first lap runs every row in the order written; each later lap skips the
// once-only rows (and their waits).
enum class Lap : uint8_t { first, repeat };

// Script-time start of every row at speed 1.0, per lap. `first` and `repeat`
// each have one entry past the last row, which holds the lap's length.
struct Timeline {
    struct Run {
        int64_t start;
        int64_t end;
        bool once;
    };

    std::vector<int64_t> first{0};
    std::vector<int64_t> repeat{0};
    std::vector<uint32_t> repeat_row; // repeat[n] is script.rows[repeat_row[n]]
    std::vector<Run> first_runs;      // the first lap as spans of once / repeated rows

    [[nodiscard]] const std::vector<int64_t>& starts(const Lap lap) const noexcept {
        return lap == Lap::first ? first : repeat;
    }
    [[nodiscard]] size_t rows(const Lap lap) const noexcept { return starts(lap).size() - 1; }
    [[nodiscard]] int64_t duration(const Lap lap) const noexcept { return starts(lap).back(); }
    // Index into script.rows of the n-th row of `lap`.
    [[nodiscard]] size_t script_row(const Lap lap, const size_t n) const noexcept {
        return lap == Lap::first ? n : repeat_row[n];
    }
    [[nodiscard]] size_t once_rows() const noexcept {
        return rows(Lap::first) - rows(Lap::repeat);
    }
    // First row that starts at or after `time_ns`; rows() when none does.
    [[nodiscard]] size_t row_at(Lap lap, int64_t time_ns) const noexcept;
};

Timeline build_timeline(const EventScript& script);

// --- Row formatting, shared with the recorder ----------------------------

inline void append_touch_row(std::string& out, const int finger_id, const bool state,
                             const int32_t x, const int32_t y, const double wait_ms) {
    char line[96];
    const int written = std::snprintf(line, sizeof line, "t,%d,%d,%d,%d,%.3f\n", finger_id,
                                      state ? 1 : 0, x, y, wait_ms);
    if (written > 0)
        out.append(line, static_cast<size_t>(written));
}

// A touch row with positions as fractions 0..1, for "@coords normalized".
inline void append_touch_row_fraction(std::string& out, const int finger_id, const bool state,
                                      const double x, const double y, const double wait_ms) {
    char line[112];
    const int written = std::snprintf(line, sizeof line, "t,%d,%d,%.6f,%.6f,%.3f\n", finger_id,
                                      state ? 1 : 0, x, y, wait_ms);
    if (written > 0)
        out.append(line, static_cast<size_t>(written));
}

inline void append_key_row(std::string& out, const uint16_t usage, const bool down,
                           const double wait_ms) {
    char line[64];
    const int written = std::snprintf(line, sizeof line, "k,0x%02x,%d,%.3f\n",
                                      static_cast<unsigned>(usage), down ? 1 : 0, wait_ms);
    if (written > 0)
        out.append(line, static_cast<size_t>(written));
}

} // namespace aoap
