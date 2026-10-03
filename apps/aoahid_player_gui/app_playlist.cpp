// SPDX-License-Identifier: MIT
//
// The Player tab's Playlist mode: the file card, the step list, and the
// playlist picker.

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

void App::draw_playlist_file_card() {
    ui::begin_card("##playlist_file");
    caption_row("Playlist");
    const float button = ImGui::GetFrameHeight();
    ui::align_right(button);
    if (ui::icon_button("##rescan_playlists", ui::Icon::refresh, button, ui::Tone::quiet,
                        "Rescan the playlists folder"))
        refresh_playlists();

    // The open playlist; clicking it opens the picker below it, the same way
    // the Script box opens the script picker.
    const bool playing = engine_.phase() == Phase::playing;
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight() + px(12);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + width, p0.y + height);
    ImGui::BeginDisabled(playing);
    const bool clicked = ImGui::InvisibleButton("##playlist_selector", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::EndDisabled();
    if (playing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Stop playback to open another playlist");
    const bool picker_open = ImGui::IsPopupOpen("##playlist_picker");
    if (clicked && !playing) {
        ImGui::OpenPopup("##playlist_picker");
        playlist_focus_search_ = true;
        playlist_pending_delete_.clear();
        playlist_cursor_ = 0;
        for (size_t index = 0; index < playlist_files_.size(); ++index) {
            if (playlist_files_[index].name == playlist_.name)
                playlist_cursor_ = static_cast<int>(index);
        }
        playlist_filter_.clear();
        playlist_follow_ = true;
    }

    ImDrawList* list = ImGui::GetWindowDrawList();
    const float rounding = px(8);
    ImGui::BeginDisabled(playing);
    list->AddRectFilled(p0, p1,
                        ImGui::GetColorU32(hovered || picker_open ? theme::field_hover
                                                                  : theme::field),
                        rounding);
    if (picker_open)
        list->AddRect(p0, p1, ImGui::GetColorU32(theme::accent_line), rounding);
    const float text_y = p0.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
    char count[32];
    std::snprintf(count, sizeof count, "%zu %s", playlist_files_.size(),
                  playlist_files_.size() == 1 ? "playlist" : "playlists");
    const float count_width = ImGui::CalcTextSize(count).x;
    const float name_width = width - px(14) - count_width - px(52);
    if (!playlist_.name.empty()) {
        ImGui::PushFont(nullptr, theme::font_title);
        ui::draw_text_ellipsized(
            list, ImVec2(p0.x + px(14), p0.y + (height - ImGui::GetTextLineHeight()) * 0.5f),
            ImGui::GetColorU32(theme::text), playlist_.name.c_str(), name_width);
        ImGui::PopFont();
    } else {
        ui::draw_text_ellipsized(list, ImVec2(p0.x + px(14), text_y),
                                 ImGui::GetColorU32(theme::text_faint),
                                 playlist_files_.empty() ? "No playlists yet: add scripts and save"
                                                         : "Choose a playlist",
                                 name_width);
    }
    list->AddText(ImVec2(p1.x - px(40) - count_width, text_y),
                  ImGui::GetColorU32(theme::text_faint), count);
    ui::draw_icon(list, ui::Icon::chevron_down, ImVec2(p1.x - px(20), p0.y + height * 0.5f),
                  px(20), ImGui::GetColorU32(theme::text_dim));
    ImGui::EndDisabled();

    ImGui::SetNextWindowPos(ImVec2(p0.x, p1.y + px(4)));
    draw_playlist_picker(width);

    // The name it is saved under.
    gap(2);
    field("Name");
    const float save_width = px(76);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - save_width -
                            ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::InputTextWithHint("##playlist_name", "Untitled", &playlist_name_input_))
        playlist_dirty_ = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(playlist_.entries.empty() || playlist_name_input_.empty());
    if (ui::button("Save", ImVec2(save_width, 0), ui::Tone::primary))
        save_current_playlist();
    ImGui::EndDisabled();

    if (!playlist_error_.empty())
        small_colored(theme::danger, playlist_error_);
    else if (playlist_dirty_ && !playlist_.entries.empty())
        small_colored(theme::warning, "Unsaved changes.");
    else {
        small_dim("Kept in the playlists folder next to the program.");
        ImGui::SetItemTooltip("%s", aoap::path_utf8(playlist_directory()).c_str());
    }
    ui::end_card();
}

void App::draw_playlist_scripts_card() {
    const bool running = engine_.phase() == Phase::playing;
    const PlaylistProgress progress = engine_.playlist_progress();
    const float button = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    // The list takes what is left of the pane and scrolls inside itself, so
    // the file card and the play controls above it stay where they are.
    ui::begin_card("##playlist_steps", nullptr,
                   std::max(ImGui::GetContentRegionAvail().y, px(180)));
    caption_row("Scripts");
    char total[64];
    int64_t loops_total = 0;
    for (const Playlist::Entry& entry : playlist_.entries)
        loops_total += entry.loops;
    std::snprintf(total, sizeof total, "%zu %s  ·  %lld %s", playlist_.entries.size(),
                  playlist_.entries.size() == 1 ? "script" : "scripts",
                  static_cast<long long>(loops_total), loops_total == 1 ? "loop" : "loops");
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

    if (!running)
        playlist_shown_step_ = SIZE_MAX;
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
            if (playlist_shown_step_ != index) {
                ImGui::SetScrollHereY(0.5f);
                playlist_shown_step_ = index;
            }
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
        // Wide enough for the number beside InputInt's own -/+ buttons.
        const float loops_width = px(116);
        const std::string name = aoap::display_name(entry.script);
        const float name_width = std::max(
            ImGui::GetContentRegionAvail().x - loops_width - button * 3 - px(24), px(40));
        const ImVec2 name_pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(name_width, ImGui::GetFrameHeight()));
        ui::draw_text_ellipsized(
            list, ImVec2(name_pos.x, name_pos.y + ImGui::GetStyle().FramePadding.y),
            ImGui::GetColorU32(missing ? theme::danger : theme::text), name.c_str(), name_width);
        if (missing)
            ImGui::SetItemTooltip("This file is no longer in the csv folder.");
        else
            ImGui::SetItemTooltip("%s", name.c_str());

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

    // Stops the whole run after this long, whichever script is playing.
    thin_rule(2.0f, 4.0f);
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
    ImGui::TextDisabled(playlist_.time_limit_minutes == 0 ? "min (0 = no limit)"
                                                          : "min, then it stops");
    ImGui::SetItemTooltip("Stops the whole playlist after this many minutes; 0 plays all of it.");
    ui::end_card();
}

// The same picker as the script one (see draw_script_picker()): a search
// box, a list moved through with the keys or the mouse, and a delete with an
// inline confirmation.
void App::draw_playlist_picker(const float width) {
    ImGui::SetNextWindowSize(ImVec2(width, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(8), px(8)));
    const bool open = ImGui::BeginPopup("##playlist_picker", ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();
    if (!open)
        return;

    // Filter first so the keys below act on what is shown.
    const std::vector<std::string> terms = search_terms(playlist_filter_);
    std::vector<int> shown;
    shown.reserve(playlist_files_.size());
    for (size_t index = 0; index < playlist_files_.size(); ++index) {
        bool match = true;
        for (const std::string& term : terms)
            match = match && playlist_files_[index].lower.find(term) != std::string::npos;
        if (match)
            shown.push_back(static_cast<int>(index));
    }

    // Escape cancels a pending delete, clears text being typed (the field
    // does that itself), and otherwise closes the picker.
    const bool escape = ui::escape_pressed();
    const bool field_clears = playlist_typing_ && !playlist_filter_.empty();
    const std::string before = playlist_filter_;
    const bool entered = ui::search_box("##search", &playlist_filter_,
                                        ImGui::GetContentRegionAvail().x, "Search playlists",
                                        playlist_focus_search_, &playlist_typing_);
    playlist_focus_search_ = false;
    if (playlist_filter_ != before) {
        playlist_cursor_ = 0;
        playlist_follow_ = true;
        playlist_pending_delete_.clear();
    }

    // The cursor is an index into `shown`.
    int cursor = -1;
    if (!shown.empty()) {
        if (playlist_filter_ == before) {
            auto found = std::find(shown.begin(), shown.end(), playlist_cursor_);
            cursor = found != shown.end() ? static_cast<int>(found - shown.begin()) : 0;
        } else {
            cursor = 0;
        }
    }
    if (cursor >= 0 && playlist_pending_delete_.empty()) {
        const int last = static_cast<int>(shown.size()) - 1;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            cursor = std::min(cursor + 1, last);
            playlist_follow_ = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            cursor = std::max(cursor - 1, 0);
            playlist_follow_ = true;
        }
        playlist_cursor_ = shown[static_cast<size_t>(cursor)];
    }
    if (escape) {
        if (!playlist_pending_delete_.empty())
            playlist_pending_delete_.clear();
        else if (!field_clears)
            ImGui::CloseCurrentPopup();
    }

    std::string choose;
    std::string remove; // deleted after the list is drawn
    if (entered && cursor >= 0 && playlist_pending_delete_.empty())
        choose = playlist_files_[static_cast<size_t>(playlist_cursor_)].name;

    // The list: up to eight rows are visible, the rest scroll.
    const float row = px(44);
    const float gap_y = px(2);
    const size_t visible = std::clamp<size_t>(shown.size(), 1, 8);
    const float list_height = static_cast<float>(visible) * (row + gap_y) - gap_y;
    gap(2);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##rows", ImVec2(0, list_height), ImGuiChildFlags_None);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, gap_y));
    if (shown.empty()) {
        ImGui::SetCursorPos(ImVec2(px(10), (row - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::TextDisabled("%s", playlist_files_.empty() ? "No playlist has been saved yet."
                                                          : "No playlist matches this search.");
    }
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    const bool mouse_moved = delta.x != 0.0f || delta.y != 0.0f;
    for (size_t position = 0; position < shown.size(); ++position) {
        const PlaylistFile& file = playlist_files_[static_cast<size_t>(shown[position])];
        const bool loaded = file.name == playlist_.name;
        const bool deleting = playlist_pending_delete_ == file.name;
        ImGui::PushID(file.name.c_str());
        const ImVec2 r0 = ImGui::GetCursorScreenPos();
        const float row_width = ImGui::GetContentRegionAvail().x;
        const ImVec2 r1(r0.x + row_width, r0.y + row);
        const bool mouse_here =
            ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(r0, r1, false);
        if (mouse_here && mouse_moved)
            playlist_cursor_ = shown[position];
        const bool current = playlist_cursor_ == shown[position];

        const ImU32 fill = deleting ? theme::danger_soft
                           : current ? theme::field_hover
                                     : 0;
        if (fill != 0)
            list->AddRectFilled(r0, r1, ImGui::GetColorU32(fill), px(6));
        if (loaded)
            list->AddRectFilled(ImVec2(r0.x, r0.y + px(10)), ImVec2(r0.x + px(3), r1.y - px(10)),
                                ImGui::GetColorU32(theme::accent), px(1.5f));

        const float line = ImGui::GetTextLineHeight();
        const float name_y = r0.y + row * 0.5f - line + px(1);
        float x = r0.x + px(14);
        if (deleting) {
            const std::string question = "Delete " + file.name + "?";
            list->AddText(ImVec2(x, r0.y + (row - line) * 0.5f), ImGui::GetColorU32(theme::text),
                          question.c_str());
        } else {
            // The name, with the first search term picked out.
            size_t hit = std::string::npos;
            size_t hit_length = 0;
            if (!terms.empty()) {
                hit = file.lower.find(terms.front());
                hit_length = terms.front().size();
            }
            const char* text = file.name.c_str();
            const auto segment = [&](const char* begin, const char* end, const ImU32 color) {
                if (begin == end)
                    return;
                list->AddText(ImVec2(x, name_y), ImGui::GetColorU32(color), begin, end);
                x += ImGui::CalcTextSize(begin, end).x;
            };
            const ImU32 base = loaded || current ? theme::text : theme::text_dim;
            if (hit == std::string::npos) {
                segment(text, text + file.name.size(), base);
            } else {
                segment(text, text + hit, base);
                segment(text + hit, text + hit + hit_length, theme::accent2_text);
                segment(text + hit + hit_length, text + file.name.size(), base);
            }
            ImGui::PushFont(nullptr, theme::font_small);
            const std::string meta = loaded ? "Open  \xC2\xB7  " + file.meta : file.meta;
            list->AddText(ImVec2(r0.x + px(14), r0.y + row * 0.5f + px(3)),
                          ImGui::GetColorU32(theme::text_faint), meta.c_str());
            ImGui::PopFont();
        }

        // Row actions go in before the row itself, so they always win the
        // click over it, even when the pointer arrives and clicks at once.
        const float button = ImGui::GetFrameHeight();
        if (deleting) {
            const float confirm_w = px(78);
            const float cancel_w = px(70);
            ImGui::SetCursorScreenPos(
                ImVec2(r1.x - confirm_w - cancel_w - px(14), r0.y + (row - button) * 0.5f));
            if (ui::button("Cancel", ImVec2(cancel_w, 0)))
                playlist_pending_delete_.clear();
            ImGui::SameLine(0, px(6));
            if (ui::button("Delete", ImVec2(confirm_w, 0), ui::Tone::danger)) {
                remove = file.name;
                playlist_pending_delete_.clear();
            }
        } else if (current) {
            ImGui::SetCursorScreenPos(ImVec2(r1.x - button - px(8), r0.y + (row - button) * 0.5f));
            if (ui::icon_button("##delete", ui::Icon::trash, button, ui::Tone::quiet,
                                "Delete this playlist"))
                playlist_pending_delete_ = file.name;
        }

        ImGui::SetCursorScreenPos(r0);
        const bool pressed = ImGui::InvisibleButton("##row", ImVec2(row_width, row));
        if (static_cast<int>(position) == cursor && playlist_follow_) {
            ImGui::SetScrollHereY(0.5f);
            playlist_follow_ = false;
        }
        if (pressed && !deleting && playlist_pending_delete_.empty())
            choose = file.name;
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (!remove.empty()) {
        std::string error;
        if (delete_playlist(remove, error))
            log_.message(aoap::Severity::info, "Deleted the playlist \"" + remove + "\".");
        else
            log_.message(aoap::Severity::error, error);
        refresh_playlists();
    }

    thin_rule(2.0f, 4.0f);
    ImGui::PushFont(nullptr, theme::font_small);
    ImGui::TextDisabled("Up/Down to move, Enter to open, Esc to close");
    ImGui::PopFont();

    if (!choose.empty()) {
        load_playlist_named(choose);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace gui
