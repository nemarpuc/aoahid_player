// SPDX-License-Identifier: MIT
//
// The window's chrome: header, nav rail, sidebar (and its collapsed form),
// and the activity log panel at the bottom.

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

#ifndef AOAHID_PLAYER_VERSION
#define AOAHID_PLAYER_VERSION "dev"
#endif

namespace gui {
using namespace detail;

App::ConnectionStatus App::connection_status() const {
    const Phase phase = engine_.phase();
    switch (phase) {
    case Phase::idle:
        return {"Not connected", theme::text_faint, false};
    case Phase::refreshing:
        return {"Searching", theme::accent, true};
    case Phase::connecting:
        return {"Connecting", theme::accent, true};
    case Phase::connected:
    case Phase::playing: {
        const size_t count = engine_.device_status().size();
        return {std::to_string(count) + (count == 1 ? " device" : " devices") + " connected",
                theme::success, false};
    }
    case Phase::disconnecting:
        return {"Disconnecting", theme::warning, true};
    }
    return {"Not connected", theme::text_faint, false};
}

void App::draw_header() {
    const float size = px(30);
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, theme::font_title);
    ImGui::TextUnformatted("AOA HID Player");
    ImGui::PopFont();
    ImGui::PushFont(nullptr, theme::font_small);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_faint);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(5));
    ImGui::TextUnformatted("Android Open Accessory input player  ·  v" AOAHID_PLAYER_VERSION);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::EndGroup();

    ImGui::SameLine(0, px(14));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (size - ImGui::GetFrameHeight()) * 0.5f);
    if (ui::icon_button("##theme", ui::Icon::sun, ImGui::GetFrameHeight(), ui::Tone::quiet,
                        dark_theme_ ? "Switch to the light theme" : "Switch to the dark theme")) {
        dark_theme_ = !dark_theme_;
        theme::set_mode(dark_theme_);
        theme_dirty_ = true;
    }

    // Status pills, right-aligned.
    const Phase phase = engine_.phase();
    const ConnectionStatus connection_status_now = connection_status();
    const std::string& connection = connection_status_now.text;
    const ImU32 connection_dot = connection_status_now.dot;
    const bool connection_pulse = connection_status_now.pulse;
    const aoap::PlaybackState state =
        phase == Phase::playing ? engine_.player().status().state : aoap::PlaybackState::stopped;
    const char* playback = state == aoap::PlaybackState::playing  ? "Playing"
                           : state == aoap::PlaybackState::paused ? "Paused"
                                                                  : "Stopped";
    const ImU32 playback_dot = state == aoap::PlaybackState::playing  ? theme::accent
                               : state == aoap::PlaybackState::paused ? theme::warning
                                                                      : theme::text_faint;
    const char* adb_label = adb_status_ == AdbStatus::running    ? "adb: running"
                            : adb_status_ == AdbStatus::stopped   ? "adb: stopped"
                            : adb_status_ == AdbStatus::not_found ? "adb: not found"
                                                                  : "adb: unknown";
    const ImU32 adb_dot = adb_status_ == AdbStatus::running    ? theme::success
                         : adb_status_ == AdbStatus::stopped   ? theme::text_faint
                         : adb_status_ == AdbStatus::not_found ? theme::danger
                                                                : theme::warning;
    const float spacing = px(12);
    const float divider_w = px(1) + spacing;
    float width = ui::status_item_width(adb_label) + divider_w +
                  ui::status_item_width(connection.c_str()) + divider_w +
                  ui::status_item_width(playback);
    if (recording())
        width += divider_w + ui::status_item_width("Recording");

    ImGui::PushFont(nullptr, theme::font_small);
    const float row_height = ImGui::GetTextLineHeight();
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (size - row_height) * 0.5f);
    ui::align_right(width);
    ui::status_item(adb_label, adb_dot);
    ImGui::SetItemTooltip("Whether the adb server is running. The startup read and the Devices "
                          "card's refresh button manage it automatically.");
    ImGui::SameLine(0, spacing);
    ui::status_divider();
    ImGui::SameLine(0, spacing);
    if (recording()) {
        ui::status_item("Recording", theme::danger, true);
        ImGui::SameLine(0, spacing);
        ui::status_divider();
        ImGui::SameLine(0, spacing);
    }
    ui::status_item(connection.c_str(), connection_dot, connection_pulse);
    ImGui::SameLine(0, spacing);
    ui::status_divider();
    ImGui::SameLine(0, spacing);
    ui::status_item(playback, playback_dot);
}

// The Player/Live/Playlist/Recorder switch, drawn as a left icon rail
// instead of a top segmented control: it sits in the window's own chrome
// (beside the sidebar, spanning the full body height) rather than floating
// as a pill above each screen's content.
void App::draw_nav_rail() {
    struct Item {
        const char* label;
        ui::Icon icon;
        Tab tab;
    };
    static constexpr Item items[] = {
        {"Player", ui::Icon::play, Tab::player},
        {"Live", ui::Icon::monitor, Tab::live},
        {"Playlist", ui::Icon::list, Tab::playlist},
        {"Recorder", ui::Icon::record, Tab::recorder},
    };
    const float diameter = px(40);
    ImDrawList* list = ImGui::GetWindowDrawList();
    for (const Item& item : items) {
        ImGui::PushID(item.label);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("##nav", ImVec2(diameter, diameter));
        const bool hovered = ImGui::IsItemHovered();
        const bool active = tab_ == item.tab;
        const ImVec2 c(p0.x + diameter * 0.5f, p0.y + diameter * 0.5f);
        if (active) {
            list->AddRectFilled(p0, ImVec2(p0.x + diameter, p0.y + diameter),
                                ImGui::GetColorU32(theme::accent2_soft), px(8));
        } else if (hovered) {
            list->AddRectFilled(p0, ImVec2(p0.x + diameter, p0.y + diameter),
                                ImGui::GetColorU32(theme::field_hover), px(8));
        }
        ui::draw_icon(list, item.icon, c, diameter * 0.5f,
                      ImGui::GetColorU32(active ? theme::accent2_text : theme::text_dim));
        if (hovered)
            ImGui::SetTooltip("%s", item.label);
        if (pressed && !active) {
            tab_ = item.tab;
            if (tab_ != Tab::live)
                live_capture_pointer(false);
        }
        ImGui::PopID();
        gap(4);
    }
}

// A thin drag handle between the sidebar and the main pane. Width is stored
// unscaled (like the profile settings), so it reads the same at any DPI.
void App::draw_sidebar_splitter(const float height) {
    constexpr float handle_width = 6.0f;
    constexpr float min_width = 280.0f;
    constexpr float max_width = 640.0f;
    ImGui::InvisibleButton("##sidebar_splitter", ImVec2(px(handle_width), height));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (hovered || active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (active) {
        sidebar_width_ = std::clamp(
            sidebar_width_ + ImGui::GetIO().MouseDelta.x / ImGui::GetStyle().FontScaleDpi,
            min_width, max_width);
    }
    if (hovered || active) {
        ImDrawList* list = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetItemRectMin();
        const ImVec2 p1 = ImGui::GetItemRectMax();
        const float cx = (p0.x + p1.x) * 0.5f;
        list->AddRectFilled(ImVec2(cx - px(1), p0.y), ImVec2(cx + px(1), p1.y),
                            ImGui::GetColorU32(active ? theme::accent : theme::border_strong),
                            px(1));
    }
}

// --- Sidebar ---------------------------------------------------------------

void App::draw_sidebar() {
    // Puts Devices/Profiles/Connect away once they are not needed for a
    // while, so Player/Live/Playlist/Recorder can use the full width.
    if (ui::icon_button("##collapse_sidebar", ui::Icon::collapse, ImGui::GetFrameHeight(),
                        ui::Tone::quiet, "Collapse"))
        sidebar_collapsed_ = true;
    gap(2);

    // The connect card stays pinned below; its height is last frame's.
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    const float scroll = std::max(ImGui::GetContentRegionAvail().y - connect_height_ - spacing,
                                  px(80));
    ImGui::BeginChild("##setup", ImVec2(0, scroll), ImGuiChildFlags_None);
    draw_devices_card();
    gap(2);
    draw_profiles_card();
    ImGui::EndChild();
    draw_connect_card();
    connect_height_ = ImGui::GetItemRectSize().y;
}

// The slim rail Devices/Profiles/Connect collapse to: just the way back and
// a quiet readout of the connection, since the cards themselves are put
// away (see draw_sidebar()).
void App::draw_sidebar_collapsed() {
    const float button = ImGui::GetFrameHeight();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         (ImGui::GetContentRegionAvail().x - button) * 0.5f);
    if (ui::icon_button("##expand_sidebar", ui::Icon::expand, button, ui::Tone::quiet,
                        "Devices, profiles, and Connect"))
        sidebar_collapsed_ = false;
    gap(3);

    const ConnectionStatus status = connection_status();
    const float diameter = px(10);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         (ImGui::GetContentRegionAvail().x - diameter) * 0.5f);
    ImGui::Dummy(ImVec2(diameter, diameter));
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetItemRectMin();
    const ImVec2 c(p0.x + diameter * 0.5f, p0.y + diameter * 0.5f);
    if (status.pulse) {
        const float phase = static_cast<float>(std::fmod(ImGui::GetTime(), 1.2) / 1.2);
        const float level = 0.45f + 0.55f * (0.5f + 0.5f * std::cos(phase * 2.0f * 3.14159265f));
        ImVec4 dot = theme::vec(status.dot);
        dot.w = level;
        list->AddCircleFilled(c, diameter * 0.5f, ImGui::GetColorU32(dot), 16);
    } else {
        list->AddCircleFilled(c, diameter * 0.5f, ImGui::GetColorU32(status.dot), 16);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", status.text.c_str());
}

// --- Activity log ----------------------------------------------------------

void App::draw_log(const float height) {
    ui::begin_card("##log", nullptr, height);
    const float button = ImGui::GetFrameHeight();
    if (ui::icon_button("##fold", log_open_ ? ui::Icon::chevron_down : ui::Icon::chevron_right,
                        button, ui::Tone::quiet, log_open_ ? "Hide" : "Show"))
        log_open_ = !log_open_;
    ImGui::SameLine(0, px(6));
    caption_row("Activity");

    const ActivityLog::Entry problem = log_.latest_problem();
    if (!log_open_ && !problem.text.empty()) {
        ImGui::SameLine(0, px(14));
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, problem.severity == aoap::Severity::error
                                                 ? theme::danger
                                                 : theme::warning);
        ImGui::TextUnformatted(problem.text.c_str());
        ImGui::PopStyleColor();
    }
    ui::align_right(px(64));
    if (ui::button("Clear", ImVec2(px(64), 0)))
        log_.clear();

    if (log_open_) {
        gap(2);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
        ImGui::BeginChild("##entries", ImVec2(0, 0), ImGuiChildFlags_None);
        const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f;
        // Every other row gets a faint tint, so a long run of entries stays
        // easy to scan by eye instead of reading as one solid block of text.
        ImDrawList* entries_list = ImGui::GetWindowDrawList();
        entries_list->ChannelsSplit(2);
        int row_index = 0;
        log_.visit([&](const ActivityLog::Entry& entry) {
            const ImVec2 row0 = ImGui::GetCursorScreenPos();
            const float row_width = ImGui::GetContentRegionAvail().x;
            entries_list->ChannelsSetCurrent(1);
            ImGui::PushFont(nullptr, theme::font_small);
            ImGui::TextColored(theme::vec(theme::text_faint), "%s", entry.time.c_str());
            ImGui::PopFont();
            ImGui::SameLine(0, px(10));
            const ImU32 color = entry.severity == aoap::Severity::error     ? theme::danger
                                : entry.severity == aoap::Severity::warning ? theme::warning
                                                                            : theme::text_dim;
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextWrapped("%s", entry.text.c_str());
            ImGui::PopStyleColor();
            if (row_index % 2 == 1) {
                entries_list->ChannelsSetCurrent(0);
                entries_list->AddRectFilled(
                    ImVec2(row0.x - px(4), row0.y - px(1)),
                    ImVec2(row0.x + row_width + px(4), ImGui::GetCursorScreenPos().y - px(1)),
                    ImGui::GetColorU32(theme::rgb(0xFFFFFF, 5)));
            }
            ++row_index;
        });
        entries_list->ChannelsMerge();
        const uint64_t version = log_.version();
        if (version != log_seen_ && (at_bottom || log_follow_))
            ImGui::SetScrollHereY(1.0f);
        log_follow_ = false;
        log_seen_ = version;
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ui::end_card();
}

} // namespace gui
