// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "event_script.hpp"
#include "events.hpp"

namespace aoap {

// How a recording writes touch coordinates.
//   raw           the touch panel's own values, with "# screen WxH" naming
//                 the panel's range (readable by every version)
//   virtual_space scaled into a square space of `virtual_size` (also
//                 readable by every version)
//   normalized    fractions 0..1 of the panel, under "@coords normalized"
//                 (needs a version that reads format 2)
enum class CoordMode : uint8_t { raw, virtual_space, normalized };

inline constexpr int32_t default_virtual_size = 32768;

// The --coords spellings: "raw", "virtual", "virtual=N" (2..65536), and
// "normalized". On failure `error` says what was wrong.
bool parse_coord_mode(std::string_view text, CoordMode& mode, int32_t& virtual_size,
                      std::string& error);

// The mode a recording is actually written in, once the panel's range is known.
struct RecordFormat {
    CoordMode mode{CoordMode::raw};
    int32_t panel_width{};  // 0: unknown, so rows stay raw
    int32_t panel_height{};
    int32_t virtual_size{default_virtual_size};
};

// Lines that name the coordinate space; written after the file's comments and
// before the first row. Empty when the panel's range is unknown.
std::string record_header(const RecordFormat& format);

// Appends `rows` as CSV, converting touch positions for `format`.
void append_recorded_rows(std::string& text, const std::vector<EventRecord>& rows,
                          const RecordFormat& format);

struct RecordOptions {
    std::string output_path;  // UTF-8; see resolve_record_path()
    std::string adb_serial;   // empty: the only device adb sees
    std::string input_device; // /dev/input/eventN on the phone; empty: every device
    CoordMode coords{CoordMode::raw};
    int32_t virtual_size{default_virtual_size}; // for CoordMode::virtual_space
};

// Runs `adb shell getevent -lt`, turns completed touch and key frames into
// lowercase CSV rows with measured waits, and streams them to a file the
// player can use directly. `-t` makes getevent print the timestamps the
// waits come from; a line without one falls back to the host clock.
//
// One Recorder records once: a stop() that arrives before run() is honoured.
class Recorder {
  public:
    explicit Recorder(EventSink* sink = nullptr) noexcept : sink_(sink) {}

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Blocks until stop() or until adb exits. Returns true when a file with
    // at least one row was saved; an empty recording leaves no file behind.
    bool run(const RecordOptions& options);

    // Any thread; async-signal-safe.
    void stop() noexcept { stopping_.store(true, std::memory_order_relaxed); }

    [[nodiscard]] bool active() const noexcept { return active_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint64_t rows() const noexcept { return rows_.load(std::memory_order_relaxed); }
    // Timing::now_ns() when recording started.
    [[nodiscard]] int64_t started_ns() const noexcept {
        return started_ns_.load(std::memory_order_relaxed);
    }

  private:
    EventSink* sink_;
    std::atomic<bool> stopping_{};
    std::atomic<bool> active_{};
    std::atomic<uint64_t> rows_{};
    std::atomic<int64_t> started_ns_{};
};

// "record-YYYYMMDD-HHMMSS", local time.
std::string timestamped_record_name();

// Where a recording named `name` is saved: empty uses a timestamped name, a
// bare name goes into script_directory() with ".csv" added when missing, and
// anything containing a directory is used as given.
std::string resolve_record_path(std::string_view name);

} // namespace aoap
