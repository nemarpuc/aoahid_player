// SPDX-License-Identifier: MIT
//
// The left sidebar's Devices and Profiles cards: device discovery, the
// touch/mouse/keyboard/gamepad/pen profile settings, and the Connect button.

#include "app.hpp"

#include "app_common.hpp"
#include "keymap.hpp"
#include "theme.hpp"

#include "aoahid_player/device.hpp"
#include "aoahid_player/paths.hpp"
#include "aoahid_player/timing.hpp"

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string_view>
#include <utility>

namespace gui {
using namespace detail;

void App::draw_devices_card() {
    ui::begin_card("##devices");
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

    if (devices_.empty()) {
        small_dim(phase == Phase::refreshing
                      ? "Searching..."
                      : "No device found. Connect a phone with a data cable and press refresh.");
    }
    const bool locked = settings_locked();
    const std::vector<aoap::DeviceStatus> status =
        engine_.connected() ? engine_.device_status() : std::vector<aoap::DeviceStatus>{};
    for (const aoap::DeviceEntry& device : devices_) {
        ImGui::PushID(device.key.c_str());
        bool selected = selected_.count(device.key) != 0;
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float row = ImGui::GetTextLineHeight() * 2.4f + px(8);
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::BeginDisabled(locked);
        if (ImGui::InvisibleButton("##row", ImVec2(width, row))) {
            if (selected)
                selected_.erase(device.key);
            else
                selected_.insert(device.key);
            selected = !selected;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::EndDisabled();

        ImDrawList* list = ImGui::GetWindowDrawList();
        const ImVec2 p1(p0.x + width, p0.y + row);
        // accent2 (not accent): ticking a device only chooses it for the
        // next Connect, it is not connected yet.
        list->AddRectFilled(p0, p1,
                            ImGui::GetColorU32(selected             ? theme::accent2_soft
                                               : hovered && !locked ? theme::field
                                                                    : theme::surface_hi),
                            px(8));
        if (selected)
            list->AddRect(p0, p1, ImGui::GetColorU32(theme::accent2_line), px(8));

        // Check box.
        const float box = px(18);
        const ImVec2 b0(p0.x + px(12), p0.y + (row - box) * 0.5f);
        const ImVec2 b1(b0.x + box, b0.y + box);
        ui::draw_check(list, b0, box, selected, theme::accent2);

        const float text_x = b1.x + px(12);
        const float line = ImGui::GetTextLineHeight();

        // Found up front so the two text lines below can reserve room for
        // the dot/count this draws on the right, instead of running under it.
        const aoap::DeviceStatus* device_status = nullptr;
        for (const aoap::DeviceStatus& entry : status) {
            if (entry.label == device.label) {
                device_status = &entry;
                break;
            }
        }
        std::string counts;
        float counts_width = 0.0f;
        if (device_status != nullptr) {
            counts = device_status->active ? format_count(device_status->reports) + " reports"
                                           : std::string("dropped");
            ImGui::PushFont(nullptr, theme::font_small);
            counts_width = ImGui::CalcTextSize(counts.c_str()).x;
            ImGui::PopFont();
        }
        const float dot_reserve = device_status != nullptr ? px(24) : px(12);
        const float count_reserve =
            device_status != nullptr ? px(16) + counts_width + px(12) : px(12);
        ui::draw_text_ellipsized(list, ImVec2(text_x, p0.y + row * 0.5f - line - px(1)),
                                 ImGui::GetColorU32(theme::text), device.product.c_str(),
                                 p1.x - dot_reserve - text_x);
        char detail[160];
        std::snprintf(detail, sizeof detail, "%04x:%04x%s%s", device.vendor_id, device.product_id,
                      device.serial.empty() ? "" : "  ·  ", device.serial.c_str());
        ui::draw_text_ellipsized(list, ImVec2(text_x, p0.y + row * 0.5f + px(1)),
                                 ImGui::GetColorU32(theme::text_faint), detail,
                                 p1.x - count_reserve - text_x);

        // Live state of a connected device: a dot, the report count, and the
        // last failure as a tooltip.
        if (device_status != nullptr) {
            const ImU32 dot = device_status->active ? theme::success : theme::danger;
            const ImVec2 c(p1.x - px(16), p0.y + row * 0.5f - line * 0.5f);
            list->AddCircleFilled(c, px(4), ImGui::GetColorU32(dot));
            ImGui::PushFont(nullptr, theme::font_small);
            list->AddText(
                ImVec2(p1.x - px(12) - counts_width, p0.y + row * 0.5f + px(2)),
                ImGui::GetColorU32(device_status->active ? theme::text_faint : theme::danger),
                counts.c_str());
            ImGui::PopFont();
            if (!device_status->last_error.empty() &&
                ImGui::IsMouseHoveringRect(p0, p1) && ImGui::IsWindowHovered())
                ImGui::SetTooltip("%s", device_status->last_error.c_str());
        }
        ImGui::PopID();
    }
    ui::end_card();
}

void App::draw_profiles_card() {
    ui::begin_card("##profiles");
    caption_row("Profiles");
    const bool locked = settings_locked();
    if (locked) {
        const char* note = "Locked while connected";
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

    ImGui::BeginDisabled(locked);
    struct Entry {
        const char* label;
        bool* enabled;
        void (App::*settings)();
        std::string summary;
    };
    char touch_summary[48];
    std::snprintf(touch_summary, sizeof touch_summary, "%d x %d", touch_width_, touch_height_);
    char key_summary[32];
    std::snprintf(key_summary, sizeof key_summary, "0x%02X-0x%02X", key_min_, key_max_);
    const Entry entries[] = {
        {"Touchscreen##touch", &use_touch_, &App::draw_touch_settings, touch_summary},
        {"Mouse##mouse", &use_mouse_, &App::draw_mouse_settings,
         std::to_string(mouse_buttons_) + " buttons"},
        {"Keyboard##key", &use_key_, &App::draw_key_settings, key_summary},
        {"Gamepad##gamepad", &use_gamepad_, &App::draw_gamepad_settings,
         std::to_string(pad_buttons_) + " buttons, " + std::to_string(pad_axes_.size()) + " axes"},
        {"Pen##pen", &use_pen_, &App::draw_pen_settings,
         pen_mode_ == 0 ? "on screen" : "tablet"},
    };
    bool first = true;
    for (const Entry& entry : entries) {
        if (!first)
            thin_rule(0.0f, 0.0f);
        first = false;
        if (ui::toggle(entry.label, entry.enabled))
            connect_error_.clear();
        if (!*entry.enabled || locked) {
            ImGui::PushFont(nullptr, theme::font_small);
            const float width = ImGui::CalcTextSize(entry.summary.c_str()).x;
            ImGui::PopFont();
            ui::align_right(width);
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(nullptr, theme::font_small);
            ImGui::TextColored(theme::vec(*entry.enabled ? theme::text_dim : theme::text_faint),
                               "%s",
                               entry.summary.c_str());
            ImGui::PopFont();
        }
        if (*entry.enabled && !locked) {
            gap(2);
            ImGui::Indent(px(4));
            ImGui::PushID(entry.label);
            (this->*entry.settings)();
            ImGui::PopID();
            ImGui::Unindent(px(4));
        }
    }
    ImGui::EndDisabled();
    ui::end_card();
}

void App::draw_touch_settings() {
    field("Resolution");
    const float number = px(66);
    ImGui::SetNextItemWidth(number);
    if (ImGui::InputInt("##width", &touch_width_, 0, 0))
        touch_width_ = std::clamp(touch_width_, 1, 65536);
    ImGui::SameLine(0, px(6));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("x");
    ImGui::SameLine(0, px(6));
    ImGui::SetNextItemWidth(number);
    if (ImGui::InputInt("##height", &touch_height_, 0, 0))
        touch_height_ = std::clamp(touch_height_, 1, 65536);
    ImGui::SetItemTooltip("Read automatically at startup with adb (wm size). An override size "
                          "wins; type here to set it by hand instead.");

    field("Contacts");
    ui::slider_int("##contacts", &touch_contacts_, 1, 16, " fingers");
    ImGui::SetItemTooltip("The Live tab always uses the last one (see the Live tab's active "
                          "touches list), so a script gets one fewer than this to itself.");
}

void App::draw_mouse_settings() {
    field("Buttons");
    ui::slider_int("##buttons", &mouse_buttons_, 1, 16, "");
}

void App::draw_key_settings() {
    field("Usages");
    const float number = px(62);
    ImGui::SetNextItemWidth(number);
    if (ImGui::InputScalar("##min", ImGuiDataType_S32, &key_min_, nullptr, nullptr, "0x%02X",
                           ImGuiInputTextFlags_CharsHexadecimal))
        key_min_ = std::clamp(key_min_, 0x04, 0xDF);
    ImGui::SameLine(0, px(6));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("to");
    ImGui::SameLine(0, px(6));
    ImGui::SetNextItemWidth(number);
    if (ImGui::InputScalar("##max", ImGuiDataType_S32, &key_max_, nullptr, nullptr, "0x%02X",
                           ImGuiInputTextFlags_CharsHexadecimal))
        key_max_ = std::clamp(key_max_, 0x04, 0xDF);
    ImGui::SetItemTooltip("HID keyboard usages in hex. 04 (A) to 65 (Menu) covers the usual "
                          "keys; modifiers are always included. Raise the upper end to 94 to "
                          "also forward the extra keys on a JIS (Japanese) keyboard — Henkan, "
                          "Muhenkan, Kana, Zenkaku/Hankaku, and Ro (87-94).");
}

void App::draw_gamepad_settings() {
    field("Buttons");
    ui::slider_int("##buttons", &pad_buttons_, 1, 32, "");

    field("Axis bits");
    ui::slider_int("##bits", &pad_bits_, 2, 32, " bits");

    field("D-pad");
    static const char* const dpad_modes[] = {"None", "Hat switch", "Four buttons"};
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##dpad", &pad_dpad_, dpad_modes, 3);

    field("Axes");
    ImGui::BeginGroup();
    const float button = ImGui::GetFrameHeight() * 0.8f;
    for (size_t index = 0; index < pad_axes_.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%zu", index);
        ImGui::SameLine(0, px(8));
        ImGui::TextUnformatted(axis_title(pad_axes_[index]).c_str());
        ImGui::SameLine();
        ui::align_right(button * 3 + px(8));
        ImGui::BeginDisabled(index == 0);
        if (ui::icon_button("##up", ui::Icon::up, button, ui::Tone::quiet))
            std::swap(pad_axes_[index], pad_axes_[index - 1]);
        ImGui::EndDisabled();
        ImGui::SameLine(0, px(4));
        ImGui::BeginDisabled(index + 1 == pad_axes_.size());
        if (ui::icon_button("##down", ui::Icon::down, button, ui::Tone::quiet))
            std::swap(pad_axes_[index], pad_axes_[index + 1]);
        ImGui::EndDisabled();
        ImGui::SameLine(0, px(4));
        const bool required =
            pad_axes_[index] == AOAHID_AXIS_X || pad_axes_[index] == AOAHID_AXIS_Y;
        ImGui::BeginDisabled(required);
        bool removed = false;
        if (ui::icon_button("##remove", ui::Icon::close, button, ui::Tone::quiet,
                            required ? nullptr : "Remove axis")) {
            pad_axes_.erase(pad_axes_.begin() + static_cast<std::ptrdiff_t>(index));
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
            if (std::find(pad_axes_.begin(), pad_axes_.end(), identity.role) != pad_axes_.end())
                continue;
            if (ImGui::Selectable(axis_title(identity.role).c_str()))
                pad_axes_.push_back(identity.role);
        }
        ImGui::EndCombo();
    }
    small_dim("The number is the axis column of \"a\" rows.");
    ImGui::EndGroup();
}

void App::draw_pen_settings() {
    field("Mode");
    static const char* const modes[] = {"On screen (direct)", "Tablet (indirect)"};
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##mode", &pen_mode_, modes, 2);
    small_dim(use_touch_ ? "Uses the touchscreen resolution."
                         : "Uses a 0-32767 surface the phone maps onto its screen.");
}


void App::draw_control_api_card() {
    ui::begin_card("##control_api");
    caption_row("Control API");
    bool enabled = control_api_.running();
    if (ui::toggle("Enabled##api", &enabled)) {
        if (enabled) {
            const std::string error = control_api_.start(api_port_);
            if (error.empty()) {
                api_enabled_ = true;
                log_.message(aoap::Severity::info, "Control API listening on 127.0.0.1:" +
                                                        std::to_string(api_port_) + ".");
            } else {
                log_.message(aoap::Severity::error, error);
            }
        } else {
            control_api_.stop();
            api_enabled_ = false;
        }
    }
    gap(2);
    field("Port");
    ImGui::BeginDisabled(control_api_.running());
    ImGui::SetNextItemWidth(px(90));
    if (ImGui::InputInt("##api_port", &api_port_, 0, 0))
        api_port_ = std::clamp(api_port_, 1024, 65535);
    ImGui::EndDisabled();
    if (control_api_.running())
        ImGui::SetItemTooltip("Turn it off to change the port.");
    gap(2);
    if (control_api_.running()) {
        small_dim(("Listening on 127.0.0.1:" + std::to_string(control_api_.port()) +
                   ". Any local program can reach it, with no further authentication — see "
                   "README.md's \"Control API\" section for the routes.")
                      .c_str());
    } else {
        small_dim("Off by default. Turning it on lets any local program drive this app over "
                  "plain HTTP — play/stop/seek a script, send touch/mouse/keyboard/gamepad/"
                  "media-key input, read status. Binds 127.0.0.1 only, never a public "
                  "interface, but nothing more: any program running on this machine could "
                  "reach it once it is on.");
    }
    ui::end_card();
}

void App::draw_connect_card() {
    ui::begin_card("##connect");
    const Phase phase = engine_.phase();
    const ImVec2 size(-FLT_MIN, ImGui::GetFrameHeight() + px(14));
    switch (phase) {
    case Phase::idle:
        ImGui::BeginDisabled(startup_prep_task_.valid() || retry_task_.valid());
        if (ui::button("Connect", size, ui::Tone::primary))
            connect();
        ImGui::EndDisabled();
        break;
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
    ui::end_card();
}

// --- Player ----------------------------------------------------------------

void App::draw_aoa_link_card() {
    const bool connected = engine_.connected();
#if defined(_WIN32)
    constexpr bool windows = true;
#else
    constexpr bool windows = false;
#endif
    if (!windows && !connected)
        return;

    ui::begin_card("##aoa_link");
    caption_row("AOA connection");
    const Phase phase = engine_.phase();
    const float width = px(150);
    ui::align_right(width);
    ImGui::BeginDisabled(!connected);
    if (ui::button(phase == Phase::disconnecting ? "Disconnecting...##aoa_link" : "Disconnect AOA",
                   ImVec2(width, 0), ui::Tone::danger))
        engine_.disconnect();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Unregisters the HID devices and ends the AOA connection.");
    if (windows) {
        gap(2);
        small_colored(connected ? theme::warning : theme::text_dim,
                      connected
                          ? "adb cannot see the phone while AOA is connected on Windows. "
                            "Press Disconnect AOA before using adb."
                          : "On Windows, adb and AOA cannot be used at the same time. Connecting "
                            "makes adb lose the phone until you disconnect.");
    }
    ui::end_card();
}

} // namespace gui
