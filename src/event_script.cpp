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

bool parse_row(const char prefix, std::string_view* field, const size_t count, size_t& columns,
               EventPayload& payload, std::string& reason, const bool normalized) {
    int64_t values[6]{};
    switch (prefix) {
    case 't': {
        columns = 5;
        if (count != columns && count != columns - 1) {
            reason = "touch needs finger_id,state,x,y[,wait_ms]";
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
        columns = 3;
        if (count != columns && count != columns - 1) {
            reason = "mouse move needs dx,dy[,wait_ms]";
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
        columns = 3;
        if (count != columns && count != columns - 1) {
            reason = "mouse button needs button_no,pressed[,wait_ms]";
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
        columns = 3;
        if (count != columns && count != columns - 1) {
            reason = "key needs usage_or_name,down[,wait_ms]";
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
        columns = 3;
        if (count != columns && count != columns - 1) {
            reason = "gamepad button needs button_no,pressed[,wait_ms]";
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
        columns = 3;
        if (count != columns && count != columns - 1) {
            reason = "gamepad axis needs axis_index,value[,wait_ms]";
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
        columns = 5;
        if (count != columns && count != columns - 1) {
            reason = "dpad needs up,down,right,left[,wait_ms]";
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
        columns = 6;
        if (count != columns && count != columns - 1) {
            reason = "pen needs in_range,tip,x,y,pressure[,wait_ms]";
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
    case 'c': {
        columns = 3;
        if (count != columns && count != columns - 1) {
            reason = "media key needs usage_or_name,down[,wait_ms]";
            return false;
        }
        bool down = false;
        uint16_t usage = 0;
        if (!parse_flag(field[1], down)) {
            reason = "media key down must be 0 or 1";
            return false;
        }
        if (parse_int64(field[0], values[0])) {
            if (values[0] < 0 || values[0] > 0xFFFF ||
                !media_usage_supported(static_cast<uint16_t>(values[0]))) {
                reason = "media key usage is not one the toggle profile declares";
                return false;
            }
            usage = static_cast<uint16_t>(values[0]);
        } else if (!media_usage_from_name(field[0], usage)) {
            reason = "unknown media key name \"" + std::string(field[0]) + "\"";
            return false;
        }
        payload = MediaKey{usage, down};
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
            else if constexpr (std::is_same_v<T, PenSample>)
                return uint64_t{7} << 56;
            else
                return uint64_t{8} << 56; // one Consumer field: any two rows conflict
        },
        payload);
}

namespace {

bool matches(const LapSelector& when, const uint64_t lap) noexcept {
    return lap >= when.first && lap <= when.last && (lap - when.first) % when.every == 0;
}

} // namespace

bool runs_on(const LapSelector& when, const uint64_t lap) noexcept {
    return lap >= 1 && matches(when, lap) != when.negate;
}

uint64_t last_run_before(const LapSelector& when, const uint64_t lap) noexcept {
    if (lap <= 1)
        return 0;
    const uint64_t top = lap - 1;
    if (!when.negate) {
        if (top < when.first)
            return 0;
        const uint64_t capped = std::min(top, when.last);
        return capped - (capped - when.first) % when.every;
    }
    if (!matches(when, top))
        return top;
    // `top` matches. With a step above 1 the lap before it cannot; with a
    // step of 1 every lap back to `first` matches. Either result may be 0.
    return when.every > 1 ? top - 1 : when.first - 1;
}

uint64_t next_run_from(const LapSelector& when, uint64_t lap) noexcept {
    lap = std::max<uint64_t>(lap, 1);
    if (!when.negate) {
        if (lap > when.last)
            return 0;
        if (lap <= when.first)
            return when.first;
        const uint64_t past = (lap - when.first) % when.every;
        if (past == 0)
            return lap;
        const uint64_t step = when.every - past;
        return step > when.last - lap ? 0 : lap + step;
    }
    if (!matches(when, lap))
        return lap;
    // `lap` matches. With a step above 1 the next lap cannot; with a step of
    // 1 every lap up to `last` matches.
    const uint64_t edge = when.every > 1 ? lap : when.last;
    return edge == UINT64_MAX ? 0 : edge + 1;
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

// True for a comment that reads "screen WxH", the old way to name the space.
bool screen_comment(std::string_view comment) {
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
    int32_t width = 0;
    int32_t height = 0;
    return parse_size(comment.substr(keyword.size()), width, height);
}

// Splits on spaces and tabs into at most `capacity` words. Returns the
// count, or capacity + 1 when there are more.
size_t split_words(std::string_view text, std::string_view* words, const size_t capacity) {
    size_t count = 0;
    while (true) {
        text = trim(text);
        if (text.empty())
            return count;
        if (count == capacity)
            return capacity + 1;
        const size_t space = text.find_first_of(" \t");
        words[count++] = text.substr(0, space);
        if (space == std::string_view::npos)
            return count;
        text.remove_prefix(space);
    }
}

bool parse_lap_number(std::string_view text, uint64_t& out) noexcept {
    int64_t value = 0;
    if (!parse_bounded(text, 1, INT64_MAX, value))
        return false;
    out = static_cast<uint64_t>(value);
    return true;
}

// "A", "A..B", or "A..", then optionally "keep-time". `argument` is lowercase.
bool parse_lap_block(std::string_view argument, LapSelector& out) {
    std::string_view words[2];
    const size_t count = split_words(argument, words, 2);
    if (count == 0 || count > 2 || (count == 2 && words[1] != "keep-time"))
        return false;
    LapSelector when;
    const std::string_view laps = words[0];
    const size_t dots = laps.find("..");
    if (dots == std::string_view::npos) {
        if (!parse_lap_number(laps, when.first))
            return false;
        when.last = when.first;
    } else {
        const std::string_view tail = laps.substr(dots + 2);
        if (!parse_lap_number(laps.substr(0, dots), when.first) ||
            (!tail.empty() && !parse_lap_number(tail, when.last)) || when.last < when.first)
            return false;
    }
    when.keep_time = count == 2;
    out = when;
    return true;
}

// "N", then optionally "from A", "to B", and "keep-time", in that order.
bool parse_every_block(std::string_view argument, LapSelector& out) {
    std::string_view words[6];
    size_t count = split_words(argument, words, 6);
    if (count == 0 || count > 6)
        return false;
    LapSelector when;
    if (words[count - 1] == "keep-time") {
        when.keep_time = true;
        --count;
    }
    if (count == 0 || !parse_lap_number(words[0], when.every))
        return false;
    when.first = when.every;
    size_t index = 1;
    if (index + 1 < count && words[index] == "from") {
        if (!parse_lap_number(words[index + 1], when.first))
            return false;
        index += 2;
    }
    if (index + 1 < count && words[index] == "to") {
        if (!parse_lap_number(words[index + 1], when.last))
            return false;
        index += 2;
    }
    if (index != count || when.last < when.first)
        return false;
    out = when;
    return true;
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
    segments.clear();
    segments.push_back(Segment{});
    lap_blocks = 0;
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
    bool in_block = false;
    bool seen_else = false;
    size_t block_line = 0;
    LapSelector block;
    // Rows from here on belong to a new segment with this selector.
    const auto open_segment = [&](const LapSelector& when) {
        const auto at = static_cast<uint32_t>(rows.size());
        segments.push_back(Segment{at, at, when, 0, 0});
    };
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
            if (trim(view.substr(0, comment)).empty() && screen_comment(view.substr(comment + 1)))
                fail(number, "\"# screen WxH\" was removed; write @screen WxH");
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
            if (word == "lap" || word == "every") {
                if (in_block) {
                    fail(number, "blocks cannot be nested");
                    continue;
                }
                in_block = true;
                seen_else = false;
                block_line = number;
                block = {};
                const bool ok = word == "lap" ? parse_lap_block(argument, block)
                                              : parse_every_block(argument, block);
                if (!ok)
                    fail(number, word == "lap"
                                     ? "@lap needs A, A..B, or A.. (laps count from 1), then "
                                       "optionally keep-time"
                                     : "@every needs N, then optionally from A, to B, and "
                                       "keep-time, in that order");
                ++lap_blocks;
                open_segment(block);
            } else if (word == "else") {
                if (!in_block || seen_else)
                    fail(number, "@else needs an open @lap or @every block, once");
                else if (block.keep_time)
                    fail(number, "@else cannot follow a keep-time block");
                else if (!argument.empty())
                    fail(number, "@else takes no option");
                else {
                    seen_else = true;
                    LapSelector others = block;
                    others.negate = true;
                    open_segment(others);
                }
            } else if (word == "end") {
                if (!in_block)
                    fail(number, "@end without @lap or @every");
                else
                    open_segment(LapSelector{});
                in_block = false;
            } else if (word == "once") {
                fail(number, "@once was removed; write @lap 1");
            } else if (word == "format") {
                int64_t version = 0;
                if (seen_row)
                    fail(number, "@format must come before the first row");
                else if (!parse_bounded(argument, 3, 3, version))
                    fail(number, "this version reads @format 3 only; see \"Migrating from "
                                 "format 1 and 2\" in README.md");
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
        if (prefix >= 'A' && prefix <= 'Z') {
            fail(number, "uppercase row prefixes were removed; put the row inside "
                         "@lap 1 ... @end");
            continue;
        }
        if (prefix < 'a' || prefix > 'z') {
            fail(number, "row must start with a profile letter");
            continue;
        }
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

        Segment& segment = segments.back();
        if (prefix == 'w') {
            int64_t wait_ns = 0;
            if (count != 1 || !parse_wait_ns(fields[0], wait_ns)) {
                fail(number, "wait row needs w,wait_ms (zero or a positive number)");
                continue;
            }
            // Onto the row before it, or ahead of the segment's first row.
            if (segment.begin == segment.end)
                segment.lead_ns += wait_ns;
            else
                rows.back().wait_ns += wait_ns;
            segment.duration += wait_ns;
            continue;
        }

        EventPayload payload{};
        std::string reason;
        size_t columns = 0;
        if (!parse_row(prefix, fields, count, columns, payload, reason, coords_normalized)) {
            fail(number, reason);
            continue;
        }

        int64_t wait_ns = 0;
        if (count == columns && !parse_wait_ns(fields[count - 1], wait_ns)) {
            fail(number, "wait_ms must be zero or a positive number of milliseconds");
            continue;
        }

        rows.push_back(EventRecord{payload, wait_ns});
        segment.end = static_cast<uint32_t>(rows.size());
        segment.duration += wait_ns;
    }

    if (in_block && !stopped)
        fail(block_line, "the block is not closed by @end");
    std::erase_if(segments, [](const Segment& segment) {
        return segment.begin == segment.end && segment.duration == 0;
    });
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

int64_t lap_length(const EventScript& script, const uint64_t lap) noexcept {
    int64_t total = 0;
    for (const Segment& segment : script.segments) {
        if (segment.when.keep_time || runs_on(segment.when, lap))
            total += segment.duration;
    }
    return total;
}

uint64_t next_lap(const EventScript& script, const uint64_t from, const bool rows_only) noexcept {
    uint64_t best = 0;
    const auto consider = [&best](const uint64_t lap) {
        if (lap != 0 && (best == 0 || lap < best))
            best = lap;
    };
    for (const Segment& segment : script.segments) {
        if (segment.begin != segment.end || (!rows_only && segment.duration > 0))
            consider(next_run_from(segment.when, from));
        if (!rows_only && segment.when.keep_time && segment.duration > 0) {
            // The laps it skips still take its time.
            LapSelector skipped = segment.when;
            skipped.negate = !skipped.negate;
            consider(next_run_from(skipped, from));
        }
    }
    return best;
}

void LapWalk::start(const EventScript& script, const uint64_t lap_number) noexcept {
    lap = lap_number;
    length = lap_length(script, lap_number);
    segment_ = 0;
    time_ = 0;
    done = false;
    settle(script);
}

void LapWalk::settle(const EventScript& script) noexcept {
    for (; segment_ < script.segments.size(); ++segment_) {
        const Segment& segment = script.segments[segment_];
        if (!runs_on(segment.when, lap)) {
            if (segment.when.keep_time)
                time_ += segment.duration;
            continue;
        }
        time_ += segment.lead_ns;
        if (segment.begin == segment.end)
            continue;
        row = segment.begin;
        due = time_;
        return;
    }
    row = static_cast<uint32_t>(script.rows.size());
    due = length;
    done = true;
}

void LapWalk::next(const EventScript& script) noexcept {
    time_ += script.rows[row].wait_ns;
    if (++row == script.segments[segment_].end) {
        ++segment_;
        settle(script);
    } else {
        due = time_;
    }
}

void LapWalk::seek(const EventScript& script, const uint64_t lap_number,
                   const int64_t time_ns) noexcept {
    start(script, lap_number);
    while (!done && due < time_ns)
        next(script);
}

int64_t lap_layout(const EventScript& script, const uint64_t lap, std::vector<LapSpan>& out) {
    out.clear();
    int64_t time = 0;
    for (const Segment& segment : script.segments) {
        const LapSelector& when = segment.when;
        if (!runs_on(when, lap)) {
            if (when.keep_time)
                time += segment.duration;
            continue;
        }
        const bool conditional =
            when.negate || when.first != 1 || when.last != UINT64_MAX || when.every != 1;
        const int64_t end = time + segment.duration;
        if (!out.empty() && out.back().conditional == conditional && out.back().end == time)
            out.back().end = end;
        else
            out.push_back({time, end, conditional});
        time = end;
    }
    return time;
}

} // namespace aoap
