// SPDX-License-Identifier: MIT
//
// The ADB tab: each connected device's ADB Bridge, turned on and off one by
// one after Connect.

#include "app.hpp"

#include "app_common.hpp"
#include "theme.hpp"

#include "aoahid_player/adb.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <string>
#include <utility>

namespace gui {
using namespace detail;

void App::run_adb_job(const size_t device, const std::string& serial, const bool connect) {
    adb_jobs_.push_back(
        std::async(std::launch::async, [version = bridge_version_, device, serial, connect,
                                        wake = wake_] {
            AdbJob job{version, device, serial, connect, false, {}};
            job.ok = connect ? aoap::adb_connect(serial, job.error)
                             : aoap::adb_disconnect(serial, job.error);
            wake();
            return job;
        }));
}

// adb's libusb backend (the default except on Windows) claims a phone's adb
// interface only when the phone first appears and never retries, so a server
// that saw it while a bridge held it is stopped once the interface is free;
// the next adb command starts a fresh one.
void App::restart_adb_server() {
    adb_jobs_.push_back(std::async(std::launch::async, [version = bridge_version_, wake = wake_] {
        AdbJob job{version, 0, {}, false, false, {}, true};
        job.ok = aoap::adb_kill_server(job.error);
        wake();
        return job;
    }));
}

void App::update_adb_bridges() {
    // 0 while not connected; setup_version() changes with every Connect.
    const uint64_t version = engine_.connected() ? engine_.setup_version() : 0U;
    if (version != bridge_version_) {
        // The old connection's bridges are closed with its devices; adb
        // would otherwise keep listing them as offline. Unless recording, the
        // server is restarted after Disconnect instead, which drops them too.
        if (version != 0U || recording()) {
            for (const BridgeRow& row : bridge_rows_) {
                if (!row.linked.empty())
                    run_adb_job(0, row.linked, false);
            }
        }
        bridge_rows_.clear();
        bridge_version_ = version;
    }

    std::vector<aoap::DeviceStatus> status;
    if (version != 0U)
        status = engine_.device_status();
    for (size_t index = bridge_rows_.size(); index < status.size(); ++index) {
        std::string key;
        for (const aoap::DeviceEntry& entry : devices_) {
            if (entry.label == status[index].label)
                key = entry.key;
        }
        const auto saved = adb_ports_.find(key);
        int port = saved != adb_ports_.end()
                       ? saved->second
                       : aoap::default_adb_bridge_port + static_cast<int>(index);
        const auto taken = [&](const int candidate) {
            return std::any_of(bridge_rows_.begin(), bridge_rows_.end(),
                               [&](const BridgeRow& row) { return row.port == candidate; });
        };
        while (taken(port) && port < 65535)
            ++port;
        BridgeRow row;
        row.port = port;
        bridge_rows_.push_back(std::move(row));
    }

    for (BridgeEvent& event : engine_.take_bridge_events()) {
        if (version == 0U || event.device >= bridge_rows_.size())
            continue;
        BridgeRow& row = bridge_rows_[event.device];
        row.busy = false;
        row.note.clear();
        row.note_error = false;
        if (event.adb_stopped) {
            set_adb_status(AdbStatus::stopped);
            log_.message(aoap::Severity::info,
                         "Stopped the adb server, which held the phone's adb interface.");
            // Every other bridge lost its adb connection with the server.
            for (size_t index = 0; index < bridge_rows_.size(); ++index) {
                BridgeRow& other = bridge_rows_[index];
                if (index != event.device && !other.linked.empty()) {
                    const std::string serial = std::exchange(other.linked, std::string());
                    run_adb_job(index, serial, true);
                }
            }
        }
        const std::string serial = aoap::adb_bridge_address(event.port);
        if (event.on) {
            log_.message(aoap::Severity::info, "ADB Bridge listening on " + serial + ".");
            run_adb_job(event.device, serial, true);
        } else if (!event.error.empty()) {
            row.note = "Could not start: " + event.error + ".";
            row.note_error = true;
            log_.message(aoap::Severity::warning, "ADB Bridge on " + serial + ": " + event.error +
                                                      ".");
        } else if (!recording()) {
            row.linked.clear();
            restart_adb_server();
        } else if (!row.linked.empty()) {
            run_adb_job(event.device, std::exchange(row.linked, std::string()), false);
        }
    }

    for (auto job = adb_jobs_.begin(); job != adb_jobs_.end();) {
        if (!ready(*job)) {
            ++job;
            continue;
        }
        const AdbJob result = job->get();
        job = adb_jobs_.erase(job);
        if (result.restart) {
            if (result.ok) {
                set_adb_status(AdbStatus::stopped);
                // Bridges still on lost their adb connection with the server.
                for (size_t index = 0; result.version == version && index < bridge_rows_.size();
                     ++index) {
                    BridgeRow& other = bridge_rows_[index];
                    if (!other.linked.empty())
                        run_adb_job(index, std::exchange(other.linked, std::string()), true);
                }
            } else if (aoap::adb_missing(result.error)) {
                set_adb_status(AdbStatus::not_found);
            }
            refresh_adb_devices();
            continue;
        }
        if (!result.connect) {
            refresh_adb_devices();
            continue;
        }
        if (result.version != version || result.device >= bridge_rows_.size())
            continue;
        BridgeRow& row = bridge_rows_[result.device];
        if (aoap::adb_missing(result.error)) {
            set_adb_status(AdbStatus::not_found);
            row.note = "adb not found on PATH; connect by hand once it is installed.";
            row.note_error = true;
            continue;
        }
        set_adb_status(AdbStatus::running); // `adb connect` starts the server
        // Kept even when unauthorised: the phone's prompt finishes that link.
        row.linked = result.serial;
        if (result.ok) {
            log_.message(aoap::Severity::info, "adb connected to " + result.serial + ".");
        } else {
            const bool prompt = result.error.find("authenticate") != std::string::npos;
            row.note = prompt ? "Allow USB debugging on the phone to finish connecting."
                              : result.error;
            row.note_error = !prompt;
            log_.message(aoap::Severity::warning, "adb connect " + result.serial + ": " +
                                                      result.error);
        }
        if (adb_choice_ == 0)
            prefer_adb_serial_ = result.serial;
        refresh_adb_devices();
    }
}

std::string App::request_bridge(const size_t index, const bool wanted, const int port) {
    if (!engine_.connected())
        return "Not connected.";
    if (engine_.phase() == Phase::playing)
        return "Bridges cannot be switched while a script plays.";
    const std::vector<aoap::DeviceStatus> status = engine_.device_status();
    if (index >= status.size() || index >= bridge_rows_.size())
        return "No connected device " + std::to_string(index + 1) + ".";
    const aoap::DeviceStatus& device = status[index];
    BridgeRow& row = bridge_rows_[index];
    if (!device.active)
        return "That device was dropped.";
    if (row.busy)
        return "Its bridge is already starting or stopping.";
    const bool on = device.adb_port != 0U;
    if (wanted && on && port != 0 && port != device.adb_port)
        return "The bridge is already on port " + std::to_string(device.adb_port) +
               "; turn it off first.";
    if (wanted == on)
        return {};
    const int wanted_port = wanted && port != 0 ? port : row.port;
    const bool clash = wanted && std::any_of(status.begin(), status.end(), [&](const auto& other) {
                           return &other != &device && other.adb_port == wanted_port;
                       });
    if (clash)
        return "Port " + std::to_string(wanted_port) + " is used by another phone.";
    row.note.clear();
    row.note_error = false;
    row.port = wanted_port;
    row.busy = true;
    engine_.adb_bridge(index, wanted, static_cast<uint16_t>(row.port));
    return {};
}

void App::draw_adb() {
    ui::begin_card("##adb_bridge");
    caption_row("ADB Bridge");
    gap(2);
    small_dim("Keeps adb working while this app holds the phone over USB. Hover for details.");
    ImGui::SetItemTooltip(
        "While this app holds a phone over USB, adb cannot open it on its own (never on\n"
        "Windows). Turning a phone's bridge on serves its adb on 127.0.0.1 and runs\n"
        "`adb connect`, so `adb devices`, `adb shell` and the Recorder reach it.\n"
        "Needs USB debugging on the phone. Avoid ports 5555-5585, which adb scans\n"
        "for emulators.");
    ui::end_card();
    gap(2);

    ui::begin_card("##adb_devices");
    if (!engine_.connected()) {
        small_dim("Connect a phone first; its bridge is then turned on here.");
        ui::end_card();
        return;
    }
    const bool playing = engine_.phase() == Phase::playing;
    const std::vector<aoap::DeviceStatus> status = engine_.device_status();
    const size_t count = std::min(status.size(), bridge_rows_.size());
    for (size_t index = 0; index < count; ++index) {
        const aoap::DeviceStatus& device = status[index];
        BridgeRow& row = bridge_rows_[index];
        const bool on = device.adb_port != 0U;
        const std::string serial = aoap::adb_bridge_address(on ? device.adb_port
                                                               : static_cast<uint16_t>(row.port));

        ImGui::PushID(static_cast<int>(index));
        if (index != 0)
            thin_rule(2.0f, 4.0f);

        // One line per phone: the switch named after it, its port, and the
        // command to copy once the bridge is on.
        ImGui::BeginDisabled(row.busy || playing || !device.active);
        bool wanted = on;
        if (ui::toggle((device.label + "##bridge").c_str(), &wanted) && wanted != on) {
            const std::string error = request_bridge(index, wanted);
            if (!error.empty()) {
                row.note = error;
                row.note_error = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Turn this phone's ADB Bridge on or off.");

        const float port_width = px(90);
        const float copy_width = px(124);
        const float port_label = ImGui::CalcTextSize("Port").x;
        ui::align_right(port_label + px(8) + port_width + (on ? px(8) + copy_width : 0.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Port");
        ImGui::SameLine(0, px(8));
        ImGui::BeginDisabled(on || row.busy);
        ImGui::SetNextItemWidth(port_width);
        if (ImGui::InputInt("##port", &row.port, 0, 0)) {
            row.port = std::clamp(row.port, 1024, 65535);
            for (const aoap::DeviceEntry& entry : devices_) {
                if (entry.label == device.label)
                    adb_ports_[entry.key] = row.port;
            }
        }
        ImGui::EndDisabled();
        if (on) {
            ImGui::SameLine(0, px(8));
            const std::string command = "adb -s " + serial + " shell";
            if (ui::button("Copy command", ImVec2(copy_width, 0))) {
                ImGui::SetClipboardText(command.c_str());
                log_.message(aoap::Severity::info, "Copied: " + command);
            }
            ImGui::SetItemTooltip("%s", command.c_str());
        }

        if (!device.active) {
            small_colored(theme::danger, "Dropped; the bridge closed with it.");
        } else if (row.busy) {
            small_dim(on ? "Stopping..." : "Starting...");
        } else if (on) {
            const std::string text =
                "Listening on " + serial +
                (row.linked.empty() ? "  ·  connecting adb..." : "  ·  adb: " + serial);
            small_dim(text.c_str());
        } else {
            small_dim("Off");
        }
        if (!row.note.empty())
            small_colored(row.note_error ? theme::danger : theme::warning, row.note);
        ImGui::PopID();
    }
    if (playing)
        small_colored(theme::warning, "Stop playback to turn a bridge on or off.");
    ui::end_card();
}

} // namespace gui
