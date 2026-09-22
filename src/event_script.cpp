// SPDX-License-Identifier: MIT
#include "aoahid_player/event_script.hpp"

#include "aoahid_player/device_group.hpp"
#include "aoahid_player/key_names.hpp"
#include "aoahid_player/paths.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <string_view>

namespace aoap {
namespace {

constexpr int64_t ns_per_ms = 1'000'000;

std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                             text.back() == '\r' || text.back() == '\n'))
        text.remove_suffix(1);
    return text;
}

// Splits on ',' into at most `capacity` trimmed fields. Returns the count, or
// capacity + 1 when there are extra columns so the caller can reject the row.
size_t split_fields(std::string_view line, std::string_view* fields, const size_t capacity) {
    size_t count = 0;
    while (true) {
        const size_t comma = line.find(',');
        std::string_view field = comma == std::string_view::npos ? line : line.substr(0, comma);
        if (count < capacity)
            fields[count] = trim(field);
        ++count;
        if (comma == std::string_view::npos)
            break;
        line.remove_prefix(comma + 1);
    }
    return count;
}

bool parse_int64(std::string_view text, int64_t& out) noexcept {
    if (text.empty())
        return false;
    int base = 10;
    bool negative = false;
    if (text.front() == '+' || text.front() == '-') {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    if (text.empty())
        return false;
    uint64_t magnitude = 0;
    const char* first = text.data();
    const char* last = first + text.size();
    const std::from_chars_result result = std::from_chars(first, last, magnitude, base);
    if (result.ec != std::errc{} || result.ptr != last)
        return false;
    const uint64_t limit = negative ? uint64_t{1} << 63 : (uint64_t{1} << 63) - 1U;
    if (magnitude > limit)
        return false;
    out = negative ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
    return true;
}

bool parse_bounded(std::string_view text, const int64_t minimum, const int64_t maximum,
                   int64_t& out) noexcept {
    return parse_int64(text, out) && out >= minimum && out <= maximum;
}

bool parse_flag(std::string_view text, bool& out) noexcept {
    int64_t value = 0;
    if (!parse_bounded(text, 0, 1, value))
        return false;
    out = value == 1;
    return true;
}

// wait_ms accepts a decimal value, matching the original aoa_touch.c script
// format. The single floating-point multiply happens here, at parse time.
bool parse_wait_ns(std::string_view text, int64_t& out) noexcept {
    if (text.empty())
        return false;
    const std::string buffer(text);
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(buffer.c_str(), &end);
    if (end == buffer.c_str() || *end != '\0' || errno == ERANGE)
        return false;
    if (!(value >= 0.0) || value > 1.0e9)
        return false;
    out = static_cast<int64_t>(std::llround(value * static_cast<double>(ns_per_ms)));
    return true;
}

// "0.5" -> an index into the 65536-wide normalized space (0..65535); 1.0
// lands exactly on the highest index, matching scale_coordinate's index
// convention.
bool parse_fraction(std::string_view text, int32_t& out) noexcept {
    if (text.empty())
        return false;
    const std::string buffer(text);
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(buffer.c_str(), &end);
    if (end == buffer.c_str() || *end != '\0' || errno == ERANGE || !(value >= 0.0) ||
        value > 1.0)
        return false;
    out = static_cast<int32_t>(std::clamp<int64_t>(
        std::llround(value * static_cast<double>(normalized_space - 1)), 0, normalized_space - 1));
    return true;
}

bool parse_row(const char prefix, std::string_view* field, const size_t count,
               EventPayload& payload, std::string& reason, const bool normalized) {
    int64_t values[6]{};
    switch (prefix) {
    case 't': {
        if (count != 5) {
            reason = "touch needs finger_id,state,x,y,wait_ms";
            return false;
        }
        bool state = false;
        int32_t x = 0;
        int32_t y = 0;
        if (!parse_bounded(field[0], 0, 65535, values[0]) || !parse_flag(field[1], state)) {
            reason = "touch column is not a valid number";
            return false;
        }
        if (normalized) {
            if (!parse_fraction(field[2], x) || !parse_fraction(field[3], y)) {
                reason = "touch x and y must be a fraction from 0 to 1 under @coords normalized";
                return false;
            }
        } else {
            if (!parse_bounded(field[2], INT32_MIN, INT32_MAX, values[2]) ||
                !parse_bounded(field[3], INT32_MIN, INT32_MAX, values[3])) {
                reason = "touch column is not a valid number";
                return false;
            }
            x = static_cast<int32_t>(values[2]);
            y = static_cast<int32_t>(values[3]);
        }
        payload = TouchEvent{static_cast<int>(values[0]), state, x, y};
        return true;
    }
    case 'm': {
        if (count != 3) {
            reason = "mouse move needs dx,dy,wait_ms";
            return false;
        }
        if (!parse_bounded(field[0], INT32_MIN, INT32_MAX, values[0]) ||
            !parse_bounded(field[1], INT32_MIN, INT32_MAX, values[1])) {
            reason = "mouse delta is not a valid number";
            return false;
        }
        payload = MouseMove{static_cast<int32_t>(values[0]), static_cast<int32_t>(values[1])};
        return true;
    }
    case 'b': {
        if (count != 3) {
            reason = "mouse button needs button_no,pressed,wait_ms";
            return false;
        }
        bool pressed = false;
        if (!parse_bounded(field[0], 1, 65535, values[0]) || !parse_flag(field[1], pressed)) {
            reason = "mouse button number must be 1-based and pressed must be 0 or 1";
            return false;
        }
        payload = MouseButton{static_cast<uint32_t>(values[0]), pressed};
        return true;
    }
    case 'k': {
        if (count != 3) {
            reason = "key needs usage_or_name,down,wait_ms";
            return false;
        }
        bool down = false;
        uint16_t usage = 0;
        if (!parse_flag(field[1], down)) {
            reason = "key down must be 0 or 1";
            return false;
        }
        // A number is a usage; anything else must be a key name.
        if (parse_int64(field[0], values[0])) {
            if (values[0] < 0 || values[0] > 0xFFFF) {
                reason = "key usage must be 0..0xffff";
                return false;
            }
            usage = static_cast<uint16_t>(values[0]);
        } else if (!key_usage_from_name(field[0], usage)) {
            reason = "unknown key name \"" + std::string(field[0]) + "\"";
            return false;
        }
        payload = KeyEvent{usage, down};
        return true;
    }
    case 'g': {
        if (count != 3) {
            reason = "gamepad button needs button_no,pressed,wait_ms";
            return false;
        }
        bool pressed = false;
        if (!parse_bounded(field[0], 1, 65535, values[0]) || !parse_flag(field[1], pressed)) {
            reason = "gamepad button number must be 1-based and pressed must be 0 or 1";
            return false;
        }
        payload = GamepadButton{static_cast<uint32_t>(values[0]), pressed};
        return true;
    }
    case 'a': {
        if (count != 3) {
            reason = "gamepad axis needs axis_index,value,wait_ms";
            return false;
        }
        if (!parse_bounded(field[0], 0, 65535, values[0]) ||
            !parse_bounded(field[1], INT32_MIN, INT32_MAX, values[1])) {
            reason = "gamepad axis index or value is not a valid number";
            return false;
        }
        payload = GamepadAxis{static_cast<size_t>(values[0]), static_cast<int32_t>(values[1])};
        return true;
    }
    case 'h': {
        if (count != 5) {
            reason = "dpad needs up,down,right,left,wait_ms";
            return false;
        }
        bool direction[4]{};
        for (size_t index = 0; index < 4; ++index) {
            if (!parse_flag(field[index], direction[index])) {
                reason = "each dpad direction must be 0 or 1";
                return false;
            }
        }
        payload = GamepadDpad{direction[0], direction[1], direction[2], direction[3]};
        return true;
    }
    case 'p': {
        if (count != 6) {
            reason = "pen needs in_range,tip,x,y,pressure,wait_ms";
            return false;
        }
        bool in_range = false;
        bool tip = false;
        int32_t x = 0;
        int32_t y = 0;
        if (!parse_flag(field[0], in_range) || !parse_flag(field[1], tip) ||
            !parse_bounded(field[4], INT32_MIN, INT32_MAX, values[4])) {
            reason = "pen column is not a valid number";
            return false;
        }
        if (normalized) {
            if (!parse_fraction(field[2], x) || !parse_fraction(field[3], y)) {
                reason = "pen x and y must be a fraction from 0 to 1 under @coords normalized";
                return false;
            }
        } else {
            if (!parse_bounded(field[2], INT32_MIN, INT32_MAX, values[2]) ||
                !parse_bounded(field[3], INT32_MIN, INT32_MAX, values[3])) {
                reason = "pen column is not a valid number";
                return false;
            }
            x = static_cast<int32_t>(values[2]);
            y = static_cast<int32_t>(values[3]);
        }
        if (tip && !in_range) {
            reason = "pen tip=1 requires in_range=1";
            return false;
        }
        payload = PenSample{in_range, tip, x, y, static_cast<int32_t>(values[4])};
        return true;
    }
    default:
        reason = "unknown row prefix";
        return false;
    }
}

} // namespace

uint64_t batch_key(const EventPayload& payload) noexcept {
    // High byte selects the control family, low bits the instance within it.
    return std::visit(
        [](const auto& value) noexcept -> uint64_t {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, TouchEvent>)
                return (uint64_t{1} << 56) | static_cast<uint64_t>(value.finger_id);
            else if constexpr (std::is_same_v<T, MouseMove>)
                return 0U; // relative deltas accumulate; batching is lossless
            else if constexpr (std::is_same_v<T, MouseButton>)
                return (uint64_t{2} << 56) | value.button;
            else if constexpr (std::is_same_v<T, KeyEvent>)
                return (uint64_t{3} << 56) | value.usage;
            else if constexpr (std::is_same_v<T, GamepadButton>)
                return (uint64_t{4} << 56) | value.button;
            else if constexpr (std::is_same_v<T, GamepadAxis>)
                return (uint64_t{5} << 56) | value.axis_index;
            else if constexpr (std::is_same_v<T, GamepadDpad>)
                return uint64_t{6} << 56;
            else
                return uint64_t{7} << 56;
        },
        payload);
}

namespace {

// "1080x2400" -> 1080, 2400.
bool parse_size(std::string_view text, int32_t& width, int32_t& height) {
    text = trim(text);
    const size_t cross = text.find_first_of("xX");
    if (cross == std::string_view::npos)
        return false;
    int32_t w = 0;
    int32_t h = 0;
    const std::string_view left = trim(text.substr(0, cross));
    const std::string_view right = trim(text.substr(cross + 1));
    const auto [end_w, error_w] = std::from_chars(left.data(), left.data() + left.size(), w);
    const auto [end_h, error_h] = std::from_chars(right.data(), right.data() + right.size(), h);
    if (error_w != std::errc{} || error_h != std::errc{} || end_w != left.data() + left.size() ||
        end_h != right.data() + right.size())
        return false;
    if (w < 1 || h < 1 || w > 65536 || h > 65536)
        return false;
    width = w;
    height = h;
    return true;
}

// "# screen 1080x2400" -> 1080, 2400. Anything else is an ordinary comment.
bool parse_screen_directive(std::string_view comment, int32_t& width, int32_t& height) {
    comment = trim(comment);
    constexpr std::string_view keyword = "screen";
    if (comment.size() <= keyword.size())
        return false;
    for (size_t index = 0; index < keyword.size(); ++index) {
        if ((comment[index] | 0x20) != keyword[index])
            return false;
    }
    if (comment[keyword.size()] != ' ' && comment[keyword.size()] != '\t')
        return false;
    return parse_size(comment.substr(keyword.size()), width, height);
}

// Coordinates are indices (0..count-1), so the highest index in `from`,
// not `from` itself, must land on the highest index in `to`.
int32_t scale_coordinate(const int32_t value, const int32_t from, const int32_t to) {
    const int64_t from_max = from - 1;
    const int64_t to_max = to - 1;
    if (from_max <= 0 || to_max <= 0)
        return 0;
    const int64_t scaled = (static_cast<int64_t>(value) * to_max + from_max / 2) / from_max;
    return static_cast<int32_t>(std::clamp<int64_t>(scaled, 0, to_max));
}

} // namespace

bool scale_script(const EventScript& script, const int32_t touch_width,
                  const int32_t touch_height, const int32_t pen_width, const int32_t pen_height,
                  EventScript& out) {
    const int32_t from_w = script.screen_width;
    const int32_t from_h = script.screen_height;
    if (from_w <= 0 || from_h <= 0)
        return false;
    const bool touch = touch_width > 0 && touch_height > 0 &&
                       (touch_width != from_w || touch_height != from_h);
    const bool pen =
        pen_width > 0 && pen_height > 0 && (pen_width != from_w || pen_height != from_h);
    if (!touch && !pen)
        return false;

    out = script;
    const auto convert = [&](std::vector<EventRecord>& rows) {
        for (EventRecord& record : rows) {
            if (auto* contact = std::get_if<TouchEvent>(&record.payload); contact && touch) {
                contact->x = scale_coordinate(contact->x, from_w, touch_width);
                contact->y = scale_coordinate(contact->y, from_h, touch_height);
            } else if (auto* sample = std::get_if<PenSample>(&record.payload); sample && pen) {
                sample->x = scale_coordinate(sample->x, from_w, pen_width);
                sample->y = scale_coordinate(sample->y, from_h, pen_height);
            }
        }
    };
    convert(out.rows);
    if (touch) {
        out.screen_width = touch_width;
        out.screen_height = touch_height;
    }
    return true;
}

bool EventScript::load(const std::string& path, std::string& error) {
    rows.clear();
    errors.clear();
    screen_width = 0;
    screen_height = 0;
    coords_normalized = false;

    const std::string name = path_utf8(utf8_path(path).filename());
    std::ifstream input(utf8_path(path), std::ios::binary);
    if (!input) {
        error = name + ": cannot open the file";
        errors.push_back(error);
        return false;
    }

    constexpr size_t max_errors = 50;
    bool stopped = false;
    const auto fail = [&](const size_t number, const std::string& reason) {
        if (errors.size() < max_errors) {
            errors.push_back(name + ':' + std::to_string(number) + ": " + reason);
        } else if (!stopped) {
            errors.push_back(name + ": too many errors; reading stopped");
            stopped = true;
        }
    };

    std::string line;
    size_t number = 0;
    bool first_line = true;
    bool seen_row = false;
    bool screen_directive = false;
    bool coords_directive = false;
    bool in_once = false;
    bool keep_time = false;
    size_t once_line = 0;
    while (!stopped && std::getline(input, line)) {
        ++number;
        std::string_view view(line);
        if (first_line && view.size() >= 3 && static_cast<unsigned char>(view[0]) == 0xEF &&
            static_cast<unsigned char>(view[1]) == 0xBB &&
            static_cast<unsigned char>(view[2]) == 0xBF) {
            view.remove_prefix(3);
        }
        first_line = false;

        const size_t comment = view.find('#');
        if (comment != std::string_view::npos) {
            if (screen_width == 0 && trim(view.substr(0, comment)).empty())
                parse_screen_directive(view.substr(comment + 1), screen_width, screen_height);
            view = view.substr(0, comment);
        }
        view = trim(view);
        if (view.empty())
            continue;

        if (view.front() == '@') {
            view.remove_prefix(1);
            const size_t space = view.find_first_of(" \t");
            std::string word(space == std::string_view::npos ? view : view.substr(0, space));
            for (char& c : word)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c | 0x20 : c);
            std::string argument_text(space == std::string_view::npos
                                          ? std::string_view()
                                          : trim(view.substr(space)));
            for (char& c : argument_text)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c | 0x20 : c);
            const std::string_view argument = argument_text;
            if (word == "once") {
                if (in_once) {
                    fail(number, "@once cannot be nested");
                    continue;
                }
                in_once = true;
                keep_time = argument == "keep-time";
                once_line = number;
                if (!argument.empty() && !keep_time)
                    fail(number, "@once takes no option except keep-time");
            } else if (word == "end") {
                if (!in_once)
                    fail(number, "@end without @once");
                in_once = false;
                keep_time = false;
            } else if (word == "format") {
                int64_t version = 0;
                if (seen_row)
                    fail(number, "@format must come before the first row");
                else if (!parse_bounded(argument, 1, 2, version))
                    fail(number, "unsupported @format (this version reads 1 and 2)");
            } else if (word == "screen") {
                int32_t w = 0;
                int32_t h = 0;
                if (seen_row)
                    fail(number, "@screen must come before the first row");
                else if (screen_directive)
                    fail(number, "@screen given twice");
                else if (coords_normalized)
                    fail(number, "@screen cannot be combined with @coords normalized");
                else if (!parse_size(argument, w, h))
                    fail(number, "@screen needs WxH, each between 1 and 65536");
                else {
                    screen_width = w;
                    screen_height = h;
                    screen_directive = true;
                }
            } else if (word == "coords") {
                if (seen_row)
                    fail(number, "@coords must come before the first row");
                else if (coords_directive)
                    fail(number, "@coords given twice");
                else if (argument == "normalized" && screen_directive)
                    fail(number, "@coords normalized cannot be combined with @screen");
                else if (argument == "normalized" || argument == "integer") {
                    coords_directive = true;
                    coords_normalized = argument == "normalized";
                } else
                    fail(number, "@coords takes normalized or integer");
            } else {
                fail(number, "unknown directive @" + word);
            }
            continue;
        }
        seen_row = true;

        const char prefix = view.front();
        const char lowered = static_cast<char>(prefix | 0x20);
        if (lowered < 'a' || lowered > 'z') {
            fail(number, "row must start with a profile letter");
            continue;
        }
        const bool once = prefix >= 'A' && prefix <= 'Z';
        view.remove_prefix(1);
        view = trim(view);
        if (view.empty() || view.front() != ',') {
            fail(number, "expected ',' after the row prefix");
            continue;
        }
        view.remove_prefix(1);

        std::string_view fields[6];
        const size_t count = split_fields(view, fields, 6);
        if (count > 6) {
            fail(number, "too many columns");
            continue;
        }

        EventPayload payload{};
        std::string reason;
        if (!parse_row(lowered, fields, count, payload, reason, coords_normalized)) {
            fail(number, reason);
            continue;
        }

        int64_t wait_ns = 0;
        if (!parse_wait_ns(fields[count - 1], wait_ns)) {
            fail(number, "wait_ms must be zero or a positive number of milliseconds");
            continue;
        }

        rows.push_back(EventRecord{payload, once || in_once, wait_ns, in_once && keep_time});
    }

    if (in_once && !stopped)
        fail(once_line, "@once is not closed by @end");
    if (coords_normalized) {
        screen_width = normalized_space;
        screen_height = normalized_space;
    }
    if (errors.empty() && rows.empty())
        errors.push_back(name + ": contains no event rows");

    if (!errors.empty()) {
        error = errors.front();
        if (errors.size() > 1)
            error += " (and " + std::to_string(errors.size() - 1) + " more)";
        return false;
    }
    return true;
}

uint32_t EventScript::required_profiles() const noexcept {
    uint32_t mask = 0;
    for (const EventRecord& record : rows)
        mask |= profile_bit(profile_of(record.payload));
    return mask;
}

size_t Timeline::row_at(const Lap lap, const int64_t time_ns) const noexcept {
    const std::vector<int64_t>& list = starts(lap);
    // The trailing entry is the lap length, not a row.
    const auto last = list.end() - 1;
    return static_cast<size_t>(std::lower_bound(list.begin(), last, time_ns) - list.begin());
}

Timeline build_timeline(const EventScript& script) {
    Timeline timeline;
    timeline.first.clear();
    timeline.repeat.clear();
    timeline.first.reserve(script.rows.size() + 1);
    timeline.repeat.reserve(script.rows.size() + 1);
    int64_t first_time = 0;
    int64_t repeat_time = 0;
    for (size_t index = 0; index < script.rows.size(); ++index) {
        const EventRecord& record = script.rows[index];
        timeline.first.push_back(first_time);
        if (!record.once) {
            timeline.repeat.push_back(repeat_time);
            timeline.repeat_row.push_back(static_cast<uint32_t>(index));
            repeat_time += record.wait_ns;
        } else if (record.keep_time) {
            repeat_time += record.wait_ns; // skipped, but the wait stays
        }
        const int64_t end = first_time + record.wait_ns;
        if (!timeline.first_runs.empty() && timeline.first_runs.back().once == record.once)
            timeline.first_runs.back().end = end;
        else
            timeline.first_runs.push_back({first_time, end, record.once});
        first_time = end;
    }
    timeline.first.push_back(first_time);
    timeline.repeat.push_back(repeat_time);
    return timeline;
}

} // namespace aoap
