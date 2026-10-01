// SPDX-License-Identifier: MIT
//
// The Recorder tab: the coordinate-mode choice and the record/stop/save
// card.

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

// --- Recorder --------------------------------------------------------------

// The two ways a recording can write touch positions, as selectable rows.
// Where a touch lands is the same in both; only the numbers differ.
void App::draw_record_coords(const bool locked) {
    struct Choice {
        const char* title;
        const char* detail;
        const char* example; // a tap in the middle of the screen
    };
    static const Choice choices[] = {
        {"Raw", "The touch panel's own numbers, as the phone reports them.",
         "Middle of the screen:  t,0,1,800,1280,16.000"},
        {"Normalized", "Fractions of the screen from 0 to 1, so 0.5 is the middle.",
         "Middle of the screen:  t,0,1,0.500000,0.500000,16.000"},
    };

    // One switch, with the chosen mode's description and example under it.
    const float half =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(locked);
    for (int index = 0; index < 2; ++index) {
        if (index != 0)
            ImGui::SameLine();
        if (ui::button(choices[index].title, ImVec2(half, 0),
                       record_coords_ == index ? ui::Tone::primary : ui::Tone::secondary))
            record_coords_ = index;
    }
    ImGui::EndDisabled();
    const Choice& chosen = choices[std::clamp(record_coords_, 0, 1)];
    small_dim(chosen.detail);
    ImGui::SetItemTooltip("If the phone's touch range cannot be read, the recording is written "
                          "raw.");
    ImGui::PushFont(nullptr, theme::font_small);
    ImGui::TextColored(theme::vec(theme::text_faint), "%s", chosen.example);
    ImGui::PopFont();
}

void App::draw_recorder() {
    const bool active = recording();

    ui::begin_card("##phone");
    caption_row("Phone");
    gap(2);
    ImGui::BeginDisabled(active);
    field("Device");
    const float button = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button -
                            ImGui::GetStyle().ItemSpacing.x);
    std::string current = "Automatic (the only phone adb sees)";
    if (adb_choice_ > 0 && static_cast<size_t>(adb_choice_) <= adb_devices_.size()) {
        const aoap::AdbDevice& device = adb_devices_[static_cast<size_t>(adb_choice_) - 1];
        current = device.model.empty() ? device.serial : device.model + "  ·  " + device.serial;
    }
    if (ImGui::BeginCombo("##adb", current.c_str())) {
        if (ImGui::Selectable("Automatic (the only phone adb sees)", adb_choice_ == 0))
            adb_choice_ = 0;
        for (size_t index = 0; index < adb_devices_.size(); ++index) {
            const aoap::AdbDevice& device = adb_devices_[index];
            std::string label = device.model.empty() ? device.serial
                                                     : device.model + "  ·  " + device.serial;
            if (device.state != "device")
                label += "  (" + device.state + ")";
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(label.c_str(), adb_choice_ == static_cast<int>(index) + 1))
                adb_choice_ = static_cast<int>(index) + 1;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (adb_task_.valid())
        ui::spinner(button * 0.4f, theme::accent);
    else if (ui::icon_button("##adb_refresh", ui::Icon::refresh, button, ui::Tone::quiet,
                             "List phones with adb devices"))
        refresh_adb_devices();

    field("Input");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##input", "Every input device (or /dev/input/eventN)",
                             &record_input_);
    ImGui::EndDisabled();
    gap(2);
    small_dim("Records touches and keys with adb over USB debugging. Hover for details.");
    ImGui::SetItemTooltip(
        "Recording reads touches and keys with adb over USB debugging. Turning a\n"
        "phone's bridge on in the ADB tab chooses its 127.0.0.1 address here. The\n"
        "result plays back with the touchscreen and keyboard profiles.");
    ui::end_card();

    gap(2);
    ui::begin_card("##coords");
    caption_row("Coordinates");
    gap(2);
    draw_record_coords(active);
    ui::end_card();

    gap(2);
    ui::begin_card("##record");
    caption_row("Recording");
    gap(2);
    if (!active) {
        // The name is chosen before recording; blank takes the date and time.
        field("File name");
        const bool typed_csv = ends_with_csv(record_name_);
        const float suffix = typed_csv ? 0.0f : ImGui::CalcTextSize(".csv").x + px(8);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - suffix);
        ImGui::InputTextWithHint("##name", "Leave blank for record-<date>-<time>", &record_name_);
        if (!typed_csv) {
            ImGui::SameLine(0, px(8));
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled(".csv");
        }
        const std::string problem = record_name_problem(record_name_);
        bool replaces = false;
        if (!problem.empty()) {
            small_colored(theme::danger, problem);
        } else {
            const std::string target =
                record_name_.empty()
                    ? aoap::path_utf8(aoap::utf8_path(aoap::script_directory()) /
                                      "record-<date>-<time>.csv")
                    : aoap::resolve_record_path(record_file_name(record_name_));
            small_dim(("Saves to " + target).c_str());
            std::error_code code;
            replaces = !record_name_.empty() &&
                       std::filesystem::exists(aoap::utf8_path(target), code);
            if (replaces)
                small_colored(theme::warning,
                              "A script with this name exists; recording replaces it.");
        }
        gap(3);
        ImGui::BeginDisabled(!problem.empty());
        if (ui::button(replaces ? "Replace and record" : "Start recording",
                       ImVec2(-FLT_MIN, ImGui::GetFrameHeight() + px(12)), ui::Tone::record))
            start_recording();
        ImGui::EndDisabled();
    } else {
        const int64_t started = recorder_ ? recorder_->started_ns() : 0;
        const bool capturing = recorder_ && recorder_->active() && started != 0;
        const int64_t elapsed = capturing ? aoap::Timing::now_ns() - started : 0;

        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushFont(nullptr, theme::font_display);
        const float line = ImGui::GetTextLineHeight();
        const float phase = static_cast<float>(std::fmod(ImGui::GetTime(), 1.2) / 1.2);
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(p.x + px(8), p.y + line * 0.5f), px(7),
            ImGui::GetColorU32(theme::rgb(0xE0564E, static_cast<unsigned>(255 - phase * 150))));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(26));
        char clock[32];
        const int64_t seconds = elapsed / 1'000'000'000;
        std::snprintf(clock, sizeof clock, "%02" PRId64 ":%02" PRId64 ".%01" PRId64, seconds / 60,
                      seconds % 60, (elapsed / 100'000'000) % 10);
        ImGui::TextUnformatted(capturing ? clock : "Starting");
        ImGui::PopFont();
        const std::string rows =
            format_count(recorder_ ? recorder_->rows() : 0) + " rows captured  ·  " +
            aoap::display_name(record_path_);
        small_dim(rows.c_str());
        small_dim(record_coords_ == 1 ? "Coordinates: normalized (0 to 1)"
                                      : "Coordinates: raw");
        gap(6);
        if (ui::button("Stop and save", ImVec2(-FLT_MIN, ImGui::GetFrameHeight() + px(12)),
                       ui::Tone::primary))
            stop_recording();
    }

    if (!last_record_path_.empty() && !active) {
        thin_rule();
        const std::string saved = "Saved " + aoap::display_name(last_record_path_) + "  ·  " +
                                  format_count(last_record_rows_) + " rows";
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(theme::success), "%s", saved.c_str());
        ui::align_right(px(130));
        ImGui::BeginDisabled(engine_.phase() == Phase::playing);
        if (ui::button("Open in player", ImVec2(px(130), 0))) {
            load_script(last_record_path_);
            tab_ = Tab::player;
            player_mode_ = PlayerMode::script;
        }
        ImGui::EndDisabled();
    }
    ui::end_card();
}

} // namespace gui
