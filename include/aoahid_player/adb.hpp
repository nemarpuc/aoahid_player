// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aoap {

// Small helpers around the `adb` command line tool, which must be on PATH.
// Every function blocks, so a GUI calls them off its UI thread. Errors are
// returned as sentences a user can act on.

struct AdbDevice {
    std::string serial;
    std::string state; // "device", "unauthorized", "offline", ...
    std::string model; // may be empty
};

// `adb [-s serial] args...`; an empty serial lets adb pick the only device.
std::vector<std::string> adb_command(const std::string& serial,
                                     const std::vector<std::string>& arguments);

bool adb_list_devices(std::vector<AdbDevice>& devices, std::string& error);

// Reads `adb shell wm size`. An active size override wins over the physical
// size, because the override is what apps (and touch input) use.
bool adb_screen_size(const std::string& serial, int32_t& width, int32_t& height,
                     std::string& error);

// The adb server keeps the phone's USB device open, which can stop the AOA
// handshake from reaching it, so it is stopped before connecting.
// `was_running`, when given, tells whether a server was actually stopped.
bool adb_kill_server(std::string& error, bool* was_running = nullptr);

// Starts the adb server if it is not already running. Used before reading
// the screen size, so `wm size` does not pay the server's own start-up cost.
bool adb_start_server(std::string& error);

// "127.0.0.1:<port>", the serial adb gives an ADB Bridge.
std::string adb_bridge_address(uint16_t port);

// True for a network serial ("host:port"), which adb reaches only after
// `adb connect`. USB serials never contain ':'.
bool adb_network_serial(std::string_view serial) noexcept;

// Runs `adb connect <address>`, starting the server when needed. adb exits 0
// even when connecting failed, so success is read from its output.
bool adb_connect(const std::string& address, std::string& error);

// Runs `adb disconnect <address>`, so adb stops listing a stopped bridge.
bool adb_disconnect(const std::string& address, std::string& error);

// True when `adb connect`'s output reports a connection ("connected to" or
// "already connected to").
bool adb_connect_succeeded(std::string_view output) noexcept;

// True when `error`, as set by a failed call above, means the `adb`
// executable itself could not be launched (missing from PATH), as opposed to
// adb running but reporting some other problem.
bool adb_missing(const std::string& error);

} // namespace aoap
