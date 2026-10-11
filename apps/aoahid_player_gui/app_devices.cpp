// SPDX-License-Identifier: MIT
//
// The Devices tab: the device list with Connect, and the HIDs and settings of
// the device picked in it.

#include "app.hpp"

#include "app_common.hpp"
#include "theme.hpp"

#include "aoahid_player/device.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace gui {
using namespace detail;

namespace {

bool has_hid(const DeviceConfig& config) noexcept {
    return config.use_touch || config.use_mouse || config.use_key || config.use_gamepad ||
           config.use_pen || config.use_digitizer || config.use_toggle;
}

// "18d1:2d00  ·  Bus 1 Port 2  ·  SERIAL"
std::string device_detail(const aoap::DeviceEntry& device) {
    std::string ports;
    for (int i = 0; i < device.port_path_length; ++i) {
        if (i > 0)
            ports += "-";
        ports += std::to_string(device.port_path[i]);
    }
    if (ports.empty())
        ports = "?";
    char detail[200];
    std::snprintf(detail, sizeof detail, "%04x:%04x  ·  Bus %d Port %s%s%s", device.vendor_id,
                  device.product_id, device.bus, ports.c_str(),
                  device.serial.empty() ? "" : "  ·  ", device.serial.c_str());
    return detail;
}

} // namespace

size_t App::connected_index(const size_t row,
                            const std::vector<aoap::DeviceStatus>& status) const {
    const std::string& key = devices_[row].key;
    size_t nth = 0;
    for (size_t before = 0; before < row; ++before)
        nth += devices_[before].key == key ? 1U : 0U;
    for (size_t index = 0; index < status.size(); ++index) {
        if (status[index].key == key && nth-- == 0)
            return index;
    }
    return SIZE_MAX;
}

void App::draw_devices() {
    const float list_width =
        std::clamp(ImGui::GetContentRegionAvail().x * 0.36f, px(280), px(420));
    ImGui::BeginChild("##device_list", ImVec2(list_width, 0), ImGuiChildFlags_None);
    draw_device_list();
    ImGui::EndChild();
    ImGui::SameLine(0, px(10));
    ImGui::BeginChild("##device_detail", ImVec2(0, 0), ImGuiChildFlags_None);
    draw_device_detail();
    ImGui::EndChild();
}

void App::draw_device_list() {
    // The Connect row stays pinned below; its height is last frame's.
    ui::begin_card("##devices", nullptr, -(connect_height_ + ImGui::GetStyle().ItemSpacing.y));
    caption_row("Devices");
    const Phase phase = engine_.phase();
    const float button = ImGui::GetFrameHeight();
    ui::align_right(button);
    if (phase == Phase::refreshing || retry_task_.valid()) {
        ui::spinner(button * 0.4f, theme::accent);
    } else {
        ImGui::BeginDisabled(phase != Phase::idle || startup_prep_task_.valid());
        if (ui::icon_button("##refresh_devices", ui::Icon::refresh, button, ui::Tone::quiet,
                            "Kill the adb server and search for AOA-capable devices again"))
            retry();
        ImGui::EndDisabled();
    }
    gap(2);

    ImDrawList* list = ImGui::GetWindowDrawList();
    const float row = ImGui::GetTextLineHeight() * 2.4f + px(8);
    const float line = ImGui::GetTextLineHeight();
    const float box = px(18);
    // The row on the right is drawn in the accent; a tick (accent2) only
    // chooses a device for the next Connect.
    const auto row_background = [&](const ImVec2 p0, const ImVec2 p1, const bool shown,
                                    const bool hovered) {
        list->AddRectFilled(p0, p1,
                            ImGui::GetColorU32(shown     ? theme::accent_soft
                                               : hovered ? theme::field
                                                         : theme::surface_hi),
                            px(8));
        if (shown)
            list->AddRect(p0, p1, ImGui::GetColorU32(theme::accent_line), px(8));
    };

    {
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        if (ImGui::InvisibleButton("##default_row", ImVec2(width, row)))
            shown_row_ = SIZE_MAX;
        const ImVec2 p1(p0.x + width, p0.y + row);
        row_background(p0, p1, shown_row_ >= devices_.size(), ImGui::IsItemHovered());
        const float text_x = p0.x + px(12) + box + px(12);
        ui::draw_text_ellipsized(list, ImVec2(text_x, p0.y + row * 0.5f - line - px(1)),
                                 ImGui::GetColorU32(theme::text), "Default", p1.x - px(12) - text_x);
        ui::draw_text_ellipsized(list, ImVec2(text_x, p0.y + row * 0.5f + px(1)),
                                 ImGui::GetColorU32(theme::text_faint),
                                 "New devices start with these settings.",
                                 p1.x - px(12) - text_x);
    }

    if (devices_.empty()) {
        gap(1);
        small_dim(phase == Phase::refreshing
                      ? "Searching..."
                      : "No device found. Connect a phone with a data cable and press refresh.");
    }
    const bool locked = phase != Phase::idle;
    const std::vector<aoap::DeviceStatus> status =
        engine_.connected() ? engine_.device_status() : std::vector<aoap::DeviceStatus>{};
    for (size_t index = 0; index < devices_.size(); ++index) {
        const aoap::DeviceEntry& device = devices_[index];
        ImGui::PushID(static_cast<int>(index));
        bool selected = selected_.count(device.key) != 0;
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemAllowOverlap();
        if (ImGui::InvisibleButton("##row", ImVec2(width, row)))
            shown_row_ = index;
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 next = ImGui::GetCursorScreenPos();
        const ImVec2 p1(p0.x + width, p0.y + row);

        // The check box is a control of its own on top of the row.
        const ImVec2 b0(p0.x + px(12), p0.y + (row - box) * 0.5f);
        const ImVec2 b1(b0.x + box, b0.y + box);
        ImGui::SetCursorScreenPos(ImVec2(b0.x - px(6), b0.y - px(6)));
        ImGui::BeginDisabled(locked);
        if (ImGui::InvisibleButton("##check", ImVec2(box + px(12), box + px(12)))) {
            if (selected)
                selected_.erase(device.key);
            else
                selected_.insert(device.key);
            selected = !selected;
            connect_error_.clear();
        }
        ImGui::EndDisabled();
        if (!locked)
            ImGui::SetItemTooltip("Connect this device");
        ImGui::SetCursorScreenPos(next);

        row_background(p0, p1, shown_row_ == index, hovered);
        ui::draw_check(list, b0, box, selected, theme::accent2);
        const float text_x = b1.x + px(12);

        const size_t connected = connected_index(index, status);
        const aoap::DeviceStatus* device_status =
            connected < status.size() ? &status[connected] : nullptr;
        std::string counts;
        if (device_status != nullptr) {
            if (device_status->active) {
                counts = device_status->profiles != 0U
                             ? aoap::describe_profiles(device_status->profiles)
                             : std::string("no HID");
                counts += "  ·  " + format_count(device_status->reports) + " reports";
                if (device_status->adb_port != 0U)
                    counts += "  ·  adb :" + std::to_string(device_status->adb_port);
            } else {
                counts = "dropped";
            }
        }
        const float dot_reserve = device_status != nullptr ? px(24) : px(12);
        ui::draw_text_ellipsized(list, ImVec2(text_x, p0.y + row * 0.5f - line - px(1)),
                                 ImGui::GetColorU32(theme::text), device.product.c_str(),
                                 p1.x - dot_reserve - text_x);
        // Connected: what it has and has received. Not connected: where it is.
        if (device_status == nullptr) {
            ui::draw_text_ellipsized(list, ImVec2(text_x, p0.y + row * 0.5f + px(1)),
                                     ImGui::GetColorU32(theme::text_faint),
                                     device_detail(device).c_str(), p1.x - px(12) - text_x);
        } else {
            const ImU32 dot = device_status->active ? theme::success : theme::danger;
            const ImVec2 c(p1.x - px(16), p0.y + row * 0.5f - line * 0.5f);
            list->AddCircleFilled(c, px(4), ImGui::GetColorU32(dot));
            ImGui::PushFont(nullptr, theme::font_small);
            ui::draw_text_ellipsized(
                list, ImVec2(text_x, p0.y + row * 0.5f + px(2)),
                ImGui::GetColorU32(device_status->active ? theme::text_faint : theme::danger),
                counts.c_str(), p1.x - px(12) - text_x);
            ImGui::PopFont();
            if (!device_status->last_error.empty() && hovered)
                ImGui::SetTooltip("%s", device_status->last_error.c_str());
        }
        ImGui::PopID();
    }
    ui::end_card();

    draw_connect_row();
    connect_height_ = ImGui::GetItemRectSize().y;
}

void App::draw_connect_row() {
    ImGui::BeginGroup();
    const Phase phase = engine_.phase();
    const ImVec2 size(-FLT_MIN, ImGui::GetFrameHeight() + px(14));
    switch (phase) {
    case Phase::idle: {
        ImGui::BeginDisabled(startup_prep_task_.valid() || retry_task_.valid());
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        bool any_selected = false;
        bool any_hid = false;
        for (const aoap::DeviceEntry& device : devices_) {
            if (selected_.count(device.key) == 0)
                continue;
            any_selected = true;
            const auto found = device_configs_.find(device.key);
            any_hid = any_hid || (found != device_configs_.end() && has_hid(found->second));
        }
        const bool no_hid = any_selected && !any_hid;
        ImGui::BeginDisabled(no_hid);
        if (ui::button("Connect", ImVec2(w, size.y), ui::Tone::primary))
            connect();
        ImGui::EndDisabled();
        if (no_hid && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Turn on at least one HID on a selected device first.");
        ImGui::SameLine();
        if (ui::button("Accessory Mode", ImVec2(w, size.y)))
            accessory();
        ImGui::EndDisabled();
        break;
    }
    case Phase::connected:
    case Phase::playing:
        if (ui::button("Disconnect", size, ui::Tone::danger))
            engine_.disconnect();
        break;
    case Phase::refreshing:
    case Phase::connecting:
    case Phase::disconnecting: {
        ImGui::BeginDisabled();
        ui::button(phase == Phase::connecting      ? "Connecting...##busy"
                   : phase == Phase::disconnecting ? "Disconnecting...##busy"
                                                   : "Searching...##busy",
                   size, ui::Tone::primary);
        ImGui::EndDisabled();
        break;
    }
    }
    if (!connect_error_.empty()) {
        gap(2);
        small_colored(theme::danger, connect_error_);
    }
    ImGui::EndGroup();
}

void App::draw_device_detail() {
    const bool is_default = shown_row_ >= devices_.size();
    const std::string key = is_default ? std::string() : devices_[shown_row_].key;
    DeviceConfig& config = is_default ? device_default_ : config_for(key);

    // The connected device this is, if it is one.
    const std::vector<aoap::DeviceStatus> status =
        engine_.connected() ? engine_.device_status() : std::vector<aoap::DeviceStatus>{};
    const size_t index = is_default ? SIZE_MAX : connected_index(shown_row_, status);
    const bool dropped = index < status.size() && !status[index].active;
    const bool connected = index < status.size() && status[index].active;
    const bool playing = engine_.phase() == Phase::playing &&
                         engine_.player().status().state != aoap::PlaybackState::paused;
    const bool busy = connected && setup_busy_.count(index) != 0;

    ui::begin_card("##device_head");
    caption_row(is_default ? "Default" : devices_[shown_row_].product.c_str());
    if (is_default)
        small_dim("What a device seen for the first time starts with. Changing it does not "
                  "change the devices already listed.");
    else
        small_dim(device_detail(devices_[shown_row_]).c_str());
    if (dropped) {
        gap(1);
        small_colored(theme::danger, "This device was dropped. Disconnect and connect again. " +
                                         status[index].last_error);
    }
    ui::end_card();
    gap(2);

    ui::begin_card("##device_hids");
    caption_row("HIDs");
    if (connected && playing) {
        const char* note = "Stop or pause playback to change";
        ImGui::PushFont(nullptr, theme::font_small);
        const float width = ImGui::CalcTextSize(note).x;
        ImGui::PopFont();
        ui::align_right(width);
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(nullptr, theme::font_small);
        ImGui::TextColored(theme::vec(theme::warning), "%s", note);
        ImGui::PopFont();
    }
    gap(2);

    // On a connected device the worker thread registers and unregisters; the
    // whole setup goes with every request, numbers that were waiting included.
    const auto send = [&] {
        if (engine_.set_device_setup(index, build_setup(config))) {
            setup_busy_.insert(index);
            return true;
        }
        return false;
    };

    ImGui::BeginDisabled(engine_.busy() || (connected && (playing || busy)));
    struct Entry {
        const char* label;
        const char* description;
        aoap::Profile profile;
        std::string summary;
    };
    char touch_summary[48];
    std::snprintf(touch_summary, sizeof touch_summary, "%d x %d", config.touch_width,
                  config.touch_height);
    char key_summary[32];
    std::snprintf(key_summary, sizeof key_summary, "0x%02X-0x%02X", config.key_min,
                  config.key_max);
    const Entry entries[] = {
        {"Touchscreen##touch", "Finger taps and swipes.", aoap::Profile::touch, touch_summary},
        {"Mouse##mouse", "Pointer movement, buttons, and the wheel.", aoap::Profile::mouse,
         std::to_string(config.mouse_buttons) + " buttons"},
        {"Keyboard##key", "Typing and key presses.", aoap::Profile::key, key_summary},
        {"Gamepad##gamepad", "Buttons, sticks, and triggers.", aoap::Profile::gamepad,
         std::to_string(config.pad_buttons) + " buttons, " +
             std::to_string(config.pad_axes.size()) + " axes"},
        {"Pen##pen", "A stylus on the screen, with pressure and hover.", aoap::Profile::pen,
         "Pen collection"},
        {"Digitizer##digitizer", "A stylus on a tablet, with pressure and hover.",
         aoap::Profile::digitizer, "Digitizer collection"},
        {"Toggle##toggle", "Media and system keys: play, volume, brightness, Home, Back.",
         aoap::Profile::toggle, "Consumer control"},
    };
    const bool unread_size = !is_default && sizes_checked_ && !config.touch_size_typed &&
                             size_read_.count(key) == 0;
    // Each row is a switch, its one-line summary, and an arrow that folds the
    // settings in and out; the settings start folded.
    const float arrow = ImGui::GetFrameHeight();
    bool first = true;
    for (const Entry& entry : entries) {
        // A hairline in the gap between two rows; it takes no room of its own.
        if (!first) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float y = std::round(p.y - ImGui::GetStyle().ItemSpacing.y * 0.5f);
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(p.x, y), ImVec2(p.x + ImGui::GetContentRegionAvail().x, y),
                ImGui::GetColorU32(theme::border));
        }
        first = false;
        bool& enabled = device_switch(config, entry.profile);
        bool& open = profile_open_[static_cast<size_t>(entry.profile)];
        if (ui::toggle(entry.label, &enabled)) {
            connect_error_.clear();
            if (connected && !send())
                enabled = !enabled;
        }
        ImGui::SetItemTooltip("%s", entry.description);

        ImGui::PushFont(nullptr, theme::font_small);
        const float summary_width = ImGui::CalcTextSize(entry.summary.c_str()).x;
        ImGui::PopFont();
        ui::align_right(summary_width + px(6) + arrow);
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(nullptr, theme::font_small);
        const bool warn = entry.profile == aoap::Profile::touch && enabled && unread_size;
        ImGui::TextColored(theme::vec(warn      ? theme::warning
                                      : enabled ? theme::text_dim
                                                : theme::text_faint),
                           "%s", entry.summary.c_str());
        if (warn)
            ImGui::SetItemTooltip("Not read from the phone; open the settings and check it.");
        ImGui::PopFont();
        ImGui::SameLine(0, px(6));
        if (enabled) {
            ImGui::PushID(entry.label);
            if (ui::icon_button("##fold", open ? ui::Icon::chevron_down : ui::Icon::chevron_right,
                                arrow, ui::Tone::quiet, open ? "Hide settings" : "Show settings"))
                open = !open;
            ImGui::PopID();
        } else {
            ImGui::Dummy(ImVec2(arrow, arrow));
        }

        if (enabled && open) {
            gap(2);
            ImGui::Indent(px(4));
            ImGui::PushID(entry.label);
            switch (entry.profile) {
            case aoap::Profile::touch:
                draw_touch_settings(config, key);
                break;
            case aoap::Profile::mouse:
                draw_mouse_settings(config);
                break;
            case aoap::Profile::key:
                draw_key_settings(config);
                break;
            case aoap::Profile::gamepad:
                draw_gamepad_settings(config);
                break;
            case aoap::Profile::pen:
            case aoap::Profile::digitizer:
                draw_stylus_settings(config);
                break;
            default:
                draw_toggle_settings();
                break;
            }
            ImGui::PopID();
            ImGui::Unindent(px(4));
        }
    }

    if (connected && !busy && build_setup(config) != status[index].setup) {
        gap(2);
        if (ui::button("Apply", ImVec2(px(90), 0), ui::Tone::primary))
            send();
        ImGui::SameLine(0, px(10));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(theme::warning), "%s",
                           "Changed settings are not applied yet.");
    } else if (busy) {
        gap(2);
        ui::spinner(ImGui::GetFrameHeight() * 0.4f, theme::accent);
        ImGui::SameLine(0, px(10));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(theme::text_dim), "%s", "Applying...");
    }
    ImGui::EndDisabled();
    ui::end_card();
}

void App::draw_touch_settings(DeviceConfig& config, const std::string& key) {
    field("Resolution");
    const float number = px(66);
    ImGui::SetNextItemWidth(number);
    if (ImGui::InputInt("##width", &config.touch_width, 0, 0)) {
        config.touch_width = std::clamp(config.touch_width, 1, INT32_MAX);
        config.touch_size_typed = true;
    }
    ImGui::SameLine(0, px(6));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("x");
    ImGui::SameLine(0, px(6));
    ImGui::SetNextItemWidth(number);
    if (ImGui::InputInt("##height", &config.touch_height, 0, 0)) {
        config.touch_height = std::clamp(config.touch_height, 1, INT32_MAX);
        config.touch_size_typed = true;
    }
    ImGui::SetItemTooltip("Read automatically at startup with adb (wm size), unless typed here.");
    // The default belongs to no phone, so there is nothing to have read.
    if (!key.empty() && !config.touch_size_typed) {
        if (size_read_.count(key) != 0)
            small_dim("Read from the phone.");
        else if (sizes_checked_)
            small_colored(theme::warning, "Not read from the phone; set it to the phone's screen "
                                          "size, or touches land in the wrong place.");
    }

    field("Contacts");
    ui::slider_int("##contacts", &config.touch_contacts, 1, 16, " fingers");
    ImGui::SetItemTooltip("The Live tab always uses the last one (see the Live tab's active "
                          "touches list), so a script gets one fewer than this to itself.");
}

void App::draw_mouse_settings(DeviceConfig& config) {
    field("Buttons");
    ui::slider_int("##buttons", &config.mouse_buttons, 1, 16, "");
}

void App::draw_key_settings(DeviceConfig& config) {
    field("Usages");
    const float number = px(62);
    ImGui::SetNextItemWidth(number);
    if (ImGui::InputScalar("##min", ImGuiDataType_S32, &config.key_min, nullptr, nullptr,
                           "0x%02X", ImGuiInputTextFlags_CharsHexadecimal))
        config.key_min = std::clamp(config.key_min, 0x04, 0xDF);
    ImGui::SameLine(0, px(6));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("to");
    ImGui::SameLine(0, px(6));
    ImGui::SetNextItemWidth(number);
    if (ImGui::InputScalar("##max", ImGuiDataType_S32, &config.key_max, nullptr, nullptr,
                           "0x%02X", ImGuiInputTextFlags_CharsHexadecimal))
        config.key_max = std::clamp(config.key_max, 0x04, 0xDF);
    ImGui::SetItemTooltip("HID keyboard usages in hex. 04 (A) to 65 (Menu) covers the usual "
                          "keys; modifiers are always included. Raise the upper end to 94 to "
                          "also forward the extra keys on a JIS (Japanese) keyboard — Henkan, "
                          "Muhenkan, Kana, Zenkaku/Hankaku, and Ro (87-94).");
}

void App::draw_gamepad_settings(DeviceConfig& config) {
    field("Buttons");
    ui::slider_int("##buttons", &config.pad_buttons, 1, 32, "");

    field("Axis bits");
    ui::slider_int("##bits", &config.pad_bits, 2, 32, " bits");

    field("D-pad");
    static const char* const dpad_modes[] = {"None", "Hat switch", "Four buttons"};
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##dpad", &config.pad_dpad, dpad_modes, 3);

    field("Axes");
    ImGui::BeginGroup();
    std::vector<aoahid_axis_role>& axes = config.pad_axes;
    const float button = ImGui::GetFrameHeight() * 0.8f;
    for (size_t index = 0; index < axes.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%zu", index);
        ImGui::SameLine(0, px(8));
        ImGui::TextUnformatted(axis_title(axes[index]).c_str());
        ImGui::SameLine();
        ui::align_right(button * 3 + px(8));
        ImGui::BeginDisabled(index == 0);
        if (ui::icon_button("##up", ui::Icon::up, button, ui::Tone::quiet))
            std::swap(axes[index], axes[index - 1]);
        ImGui::EndDisabled();
        ImGui::SameLine(0, px(4));
        ImGui::BeginDisabled(index + 1 == axes.size());
        if (ui::icon_button("##down", ui::Icon::down, button, ui::Tone::quiet))
            std::swap(axes[index], axes[index + 1]);
        ImGui::EndDisabled();
        ImGui::SameLine(0, px(4));
        const bool required = axes[index] == AOAHID_AXIS_X || axes[index] == AOAHID_AXIS_Y;
        ImGui::BeginDisabled(required);
        bool removed = false;
        if (ui::icon_button("##remove", ui::Icon::close, button, ui::Tone::quiet,
                            required ? nullptr : "Remove axis")) {
            axes.erase(axes.begin() + static_cast<std::ptrdiff_t>(index));
            removed = true;
        }
        ImGui::EndDisabled();
        ImGui::PopID();
        if (removed)
            break;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##add", "Add axis", ImGuiComboFlags_HeightLarge)) {
        for (const aoap::spec_detail::AxisIdentity& identity : aoap::spec_detail::axis_table) {
            if (std::find(axes.begin(), axes.end(), identity.role) != axes.end())
                continue;
            if (ImGui::Selectable(axis_title(identity.role).c_str()))
                axes.push_back(identity.role);
        }
        ImGui::EndCombo();
    }
    small_dim("The number is the axis column of \"a\" rows.");
    ImGui::EndGroup();
}

void App::draw_stylus_settings(DeviceConfig& config) {
    small_dim(config.use_touch ? "Uses the touchscreen resolution."
                               : "Uses a 0-32767 surface the phone maps onto its screen.");
}

void App::draw_toggle_settings() {
    small_dim("Enables media controls and system buttons (Home, Back, Volume, Play, etc.).");
}

} // namespace gui
