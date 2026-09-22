// SPDX-License-Identifier: MIT
//
// The Playlist tab: the step list, its file card, and the add-step picker.

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

// --- Playlist --------------------------------------------------------------

void App::draw_playlist() {
    const bool running = engine_.phase() == Phase::playing;
    const PlaylistProgress progress = engine_.playlist_progress();

    ui::begin_card("##playlist_file");
    caption_row("Playlist");
    const float button = ImGui::GetFrameHeight();
    ui::align_right(button);
    if (ui::icon_button("##rescan_playlists", ui::Icon::refresh, button, ui::Tone::quiet,
                        "Rescan the playlists folder"))
        refresh_playlists();
    gap(2);

    field("Name");
    const float open_width = px(86);
    const float save_width = px(76);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - open_width - save_width -
                            spacing * 2);
    if (ImGui::InputTextWithHint("##playlist_name", "Untitled", &playlist_name_input_))
        playlist_dirty_ = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(playlist_names_.empty());
    if (ui::button("Open", ImVec2(open_width, 0)))
        open_playlist_picker_ = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(playlist_.entries.empty() || playlist_name_input_.empty());
    if (ui::button("Save", ImVec2(save_width, 0), ui::Tone::primary))
        save_current_playlist();
    ImGui::EndDisabled();
    draw_playlist_picker();

    if (!playlist_error_.empty())
        small_colored(theme::danger, playlist_error_);
    else if (playlist_dirty_ && !playlist_.entries.empty())
        small_colored(theme::warning, "Unsaved changes.");
    else
        small_dim(("Playlists are kept in " + aoap::path_utf8(playlist_directory())).c_str());
    ui::end_card();

    gap(2);
    ui::begin_card("##playlist_steps");
    caption_row("Scripts");
    char total[64];
    int64_t loops_total = 0;
    for (const Playlist::Entry& entry : playlist_.entries)
        loops_total += entry.loops;
    std::snprintf(total, sizeof total, "%zu %s  ·  %lld loops", playlist_.entries.size(),
                  playlist_.entries.size() == 1 ? "script" : "scripts",
                  static_cast<long long>(loops_total));
    ImGui::PushFont(nullptr, theme::font_small);
    const float total_width = ImGui::CalcTextSize(total).x;
    ImGui::PopFont();
    ui::align_right(total_width);
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(nullptr, theme::font_small);
    ImGui::TextColored(theme::vec(theme::text_faint), "%s", total);
    ImGui::PopFont();
    gap(2);

    if (playlist_.entries.empty())
        small_dim("Add scripts below; they play in this order, each for its own number of loops.");

    // Rows: order, name, loops, remove.
    size_t remove_at = playlist_.entries.size();
    for (size_t index = 0; index < playlist_.entries.size(); ++index) {
        Playlist::Entry& entry = playlist_.entries[index];
        ImGui::PushID(static_cast<int>(index));
        const bool current = running && progress.active && progress.index == index;
        const ImVec2 r0 = ImGui::GetCursorScreenPos();
        const float row = ImGui::GetFrameHeight() + px(10);
        const float width = ImGui::GetContentRegionAvail().x;
        ImDrawList* list = ImGui::GetWindowDrawList();
        if (current) {
            list->AddRectFilled(r0, ImVec2(r0.x + width, r0.y + row),
                                ImGui::GetColorU32(theme::accent_soft), px(6));
        }

        ImGui::SetCursorScreenPos(
            ImVec2(r0.x + px(10), r0.y + (row - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(current ? theme::accent_text : theme::text_faint), "%zu",
                           index + 1);
        ImGui::SameLine(0, px(10));

        const std::string path = resolve_script_reference(entry.script);
        std::error_code code;
        const bool missing = !std::filesystem::exists(aoap::utf8_path(path), code);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, missing ? theme::danger : theme::text);
        ImGui::TextUnformatted(aoap::display_name(entry.script).c_str());
        ImGui::PopStyleColor();
        if (missing)
            ImGui::SetItemTooltip("This file is no longer in the csv folder.");

        const float loops_width = px(92);
        ui::align_right(loops_width + button * 3 + px(14));
        ImGui::SetNextItemWidth(loops_width);
        ImGui::BeginDisabled(running);
        int loops = static_cast<int>(entry.loops);
        if (ImGui::InputInt("##loops", &loops, 1, 10)) {
            entry.loops = std::clamp(loops, 1, 1000000);
            playlist_dirty_ = true;
        }
        ImGui::SetItemTooltip("How many times this script loops");
        ImGui::SameLine(0, px(8));
        ImGui::BeginDisabled(index == 0);
        if (ui::icon_button("##up", ui::Icon::up, button, ui::Tone::quiet, "Move up")) {
            std::swap(playlist_.entries[index], playlist_.entries[index - 1]);
            playlist_dirty_ = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0, px(4));
        ImGui::BeginDisabled(index + 1 == playlist_.entries.size());
        if (ui::icon_button("##down", ui::Icon::down, button, ui::Tone::quiet, "Move down")) {
            std::swap(playlist_.entries[index], playlist_.entries[index + 1]);
            playlist_dirty_ = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0, px(6));
        if (ui::icon_button("##remove", ui::Icon::close, button, ui::Tone::quiet, "Remove"))
            remove_at = index;
        ImGui::EndDisabled();
        ImGui::SetCursorScreenPos(ImVec2(r0.x, r0.y + row + px(2)));
        ImGui::PopID();
    }
    if (remove_at < playlist_.entries.size()) {
        playlist_.entries.erase(playlist_.entries.begin() +
                                static_cast<std::ptrdiff_t>(remove_at));
        playlist_dirty_ = true;
    }

    // Add a script from the csv folder.
    gap(2);
    thin_rule(0.0f, 4.0f);
    ImGui::BeginDisabled(running || scripts_.empty());
    const float add_width = px(76);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - add_width - spacing);
    const char* preview = scripts_.empty() ? "No scripts in the csv folder"
                                           : scripts_[static_cast<size_t>(std::clamp(
                                                          playlist_add_choice_, 0,
                                                          static_cast<int>(scripts_.size()) - 1))]
                                                 .name.c_str();
    if (ImGui::BeginCombo("##add_script", preview)) {
        for (size_t index = 0; index < scripts_.size(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(scripts_[index].name.c_str(),
                                  playlist_add_choice_ == static_cast<int>(index)))
                playlist_add_choice_ = static_cast<int>(index);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ui::button("Add", ImVec2(add_width, 0)) && !scripts_.empty()) {
        const size_t choice = static_cast<size_t>(
            std::clamp(playlist_add_choice_, 0, static_cast<int>(scripts_.size()) - 1));
        playlist_.entries.push_back(
            Playlist::Entry{script_reference(scripts_[choice].path), 1});
        playlist_dirty_ = true;
    }
    ImGui::EndDisabled();
    ui::end_card();

    gap(2);
    ui::begin_card("##playlist_run");
    caption_row("Run");
    gap(2);
    field("Time limit");
    ImGui::BeginDisabled(running);
    ImGui::SetNextItemWidth(px(120));
    if (ImGui::InputInt("##limit", &playlist_.time_limit_minutes, 1, 10)) {
        playlist_.time_limit_minutes = std::clamp(playlist_.time_limit_minutes, 0, 100000);
        playlist_dirty_ = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0, px(10));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled(playlist_.time_limit_minutes == 0
                            ? "minutes (0 plays the whole playlist)"
                            : "minutes, then playback stops");

    gap(6);
    if (running) {
        if (progress.active) {
            char line[160];
            std::snprintf(line, sizeof line, "Playing %zu/%zu: %s", progress.index + 1,
                          progress.count, progress.name.c_str());
            ImGui::TextUnformatted(line);
            if (progress.ends_at_ns != 0) {
                const int64_t left = progress.ends_at_ns - aoap::Timing::now_ns();
                small_dim(("Time limit: " + format_time(std::max<int64_t>(left, 0)) + " left")
                              .c_str());
            }
            gap(4);
        }
        if (ui::button("Stop playlist", ImVec2(-FLT_MIN, ImGui::GetFrameHeight() + px(12)),
                       ui::Tone::danger))
            stop_playback();
    } else {
        const bool ready_to_play = engine_.phase() == Phase::connected && playlist_playable();
        ImGui::BeginDisabled(!ready_to_play);
        if (ui::button("Play playlist", ImVec2(-FLT_MIN, ImGui::GetFrameHeight() + px(12)),
                       ui::Tone::primary))
            play_playlist();
        ImGui::EndDisabled();
        if (!ready_to_play && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", playlist_.entries.empty() ? "Add a script first"
                                    : !engine_.connected()   ? "Connect a device first"
                                                             : "One of the scripts is missing");
        }
    }
    ui::end_card();
}

void App::draw_playlist_picker() {
    if (open_playlist_picker_) {
        ImGui::OpenPopup("##playlist_picker");
        open_playlist_picker_ = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(8), px(8)));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::surface_hi);
    ImGui::PushStyleColor(ImGuiCol_Border, theme::border_strong);
    const bool open = ImGui::BeginPopup("##playlist_picker");
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    if (!open)
        return;

    std::string chosen;
    std::string remove;
    for (const std::string& name : playlist_names_) {
        ImGui::PushID(name.c_str());
        const float button = ImGui::GetFrameHeight();
        if (ImGui::Selectable("##row", name == playlist_.name, ImGuiSelectableFlags_None,
                              ImVec2(px(260), button)))
            chosen = name;
        const ImVec2 r0 = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(r0.x + px(8), r0.y + (button - ImGui::GetTextLineHeight()) * 0.5f),
            ImGui::GetColorU32(theme::text), name.c_str());
        ImGui::SameLine();
        ui::align_right(button);
        if (ui::icon_button("##delete", ui::Icon::trash, button, ui::Tone::quiet, "Delete"))
            remove = name;
        ImGui::PopID();
    }
    if (!chosen.empty()) {
        load_playlist_named(chosen);
        ImGui::CloseCurrentPopup();
    }
    if (!remove.empty()) {
        std::string error;
        if (delete_playlist(remove, error))
            log_.message(aoap::Severity::info, "Deleted the playlist \"" + remove + "\".");
        else
            log_.message(aoap::Severity::error, error);
        refresh_playlists();
    }
    ImGui::EndPopup();
}

} // namespace gui
