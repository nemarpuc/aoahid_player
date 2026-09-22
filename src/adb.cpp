// SPDX-License-Identifier: MIT
#include "aoahid_player/adb.hpp"

#include "aoahid_player/process.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace aoap {
namespace {

// The number after `label` in `adb shell wm size`'s "Physical size: WxH" /
// "Override size: WxH" lines.
bool parse_wm_size(const std::string& text, const std::string_view label, int32_t& width,
                   int32_t& height) {
    const size_t at = text.find(label);
    if (at == std::string::npos)
        return false;
    const char* begin = text.c_str() + at + label.size();
    errno = 0;
    char* mid = nullptr;
    const long w = std::strtol(begin, &mid, 10);
    if (mid == begin || *mid != 'x' || errno != 0)
        return false;
    errno = 0;
    char* end = nullptr;
    const long h = std::strtol(mid + 1, &end, 10);
    if (end == mid + 1 || errno != 0 || w <= 0 || h <= 0 || w > INT32_MAX || h > INT32_MAX)
        return false;
    width = static_cast<int32_t>(w);
    height = static_cast<int32_t>(h);
    return true;
}

std::string first_line(const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        // adb reports its own server start-up on lines starting with '*'.
        if (!line.empty() && line.front() != '*')
            return line;
    }
    return {};
}

// Turns a failed adb run into one readable sentence.
bool check(const CommandResult& result, std::string& error) {
    if (!result.started) {
        error = "adb could not be run: " + result.start_error +
                ". Install Android SDK Platform-Tools and add it to PATH.";
        return false;
    }
    if (result.timed_out) {
        error = "adb did not answer within the time limit.";
        return false;
    }
    if (result.exit_code != 0) {
        std::string reason = first_line(result.error_output);
        if (reason.empty())
            reason = first_line(result.output);
        error = reason.empty() ? "adb failed (exit code " + std::to_string(result.exit_code) + ")."
                               : "adb: " + reason;
        return false;
    }
    return true;
}

} // namespace

std::vector<std::string> adb_command(const std::string& serial,
                                     const std::vector<std::string>& arguments) {
    std::vector<std::string> command{"adb"};
    if (!serial.empty()) {
        command.emplace_back("-s");
        command.push_back(serial);
    }
    command.insert(command.end(), arguments.begin(), arguments.end());
    return command;
}

bool adb_list_devices(std::vector<AdbDevice>& devices, std::string& error) {
    devices.clear();
    const CommandResult result = run_command(adb_command({}, {"devices", "-l"}));
    if (!check(result, error))
        return false;
    std::istringstream stream(result.output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line.front() == '*' || line.rfind("List of devices", 0) == 0)
            continue;
        std::istringstream fields(line);
        AdbDevice device;
        if (!(fields >> device.serial >> device.state))
            continue;
        std::string field;
        while (fields >> field) {
            if (field.rfind("model:", 0) == 0) {
                device.model = field.substr(6);
                for (char& c : device.model)
                    c = c == '_' ? ' ' : c;
            }
        }
        devices.push_back(std::move(device));
    }
    return true;
}

bool adb_screen_size(const std::string& serial, int32_t& width, int32_t& height,
                     std::string& error) {
    const CommandResult result = run_command(adb_command(serial, {"shell", "wm", "size"}));
    if (!check(result, error))
        return false;

    if (parse_wm_size(result.output, "Override size:", width, height) ||
        parse_wm_size(result.output, "Physical size:", width, height))
        return true;

    error = "Could not read the screen size from `adb shell wm size`.";
    return false;
}

bool adb_kill_server(std::string& error) {
    return check(run_command(adb_command({}, {"kill-server"})), error);
}

bool adb_start_server(std::string& error) {
    return check(run_command(adb_command({}, {"start-server"})), error);
}

bool adb_missing(const std::string& error) {
    return error.rfind("adb could not be run", 0) == 0;
}

} // namespace aoap
