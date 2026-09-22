// SPDX-License-Identifier: MIT
//
// The Player tab: the script picker and its card, the Player action keys,
// and the transport card (timeline, play/stop/restart, speed/loop/offset).

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

void App::draw_player() {
    draw_script_card();
    gap(2);
    draw_transport_card();
}

void App::draw_script_card() {
    ui::begin_card("##script");
    caption_row("Script");
    const float button = ImGui::GetFrameHeight();
    ui::align_right(button);
    if (ui::icon_button("##rescan", ui::Icon::refresh, button, ui::Tone::quiet,
                        "Rescan the csv folder"))
        refresh_scripts();

    // The loaded script; clicking it opens the picker below it.
    const bool playing = engine_.phase() == Phase::playing;
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight() + px(12);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + width, p0.y + height);
    ImGui::BeginDisabled(playing);
    const bool clicked = ImGui::InvisibleButton("##selector", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::EndDisabled();
    if (playing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Stop playback to change the script");
    const bool picker_open = ImGui::IsPopupOpen("##picker");
    if ((clicked || open_picker_) && !playing) {
        ImGui::OpenPopup("##picker");
        focus_search_ = true;
        pending_delete_.clear();
        picker_cursor_ = 0;
        for (size_t index = 0; index < scripts_.size(); ++index) {
            if (scripts_[index].path == script_path_)
                picker_cursor_ = static_cast<int>(index);
        }
        script_filter_.clear();
        picker_follow_ = true;
    }
    open_picker_ = false;

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
    if (script_) {
        const std::string name = aoap::display_name(script_path_);
        ImGui::PushFont(nullptr, theme::font_title);
        list->AddText(ImVec2(p0.x + px(14), p0.y + (height - ImGui::GetTextLineHeight()) * 0.5f),
                      ImGui::GetColorU32(theme::text), name.c_str());
        ImGui::PopFont();
    } else {
        list->AddText(ImVec2(p0.x + px(14), text_y), ImGui::GetColorU32(theme::text_faint),
                      scripts_.empty() ? "No scripts yet: record one or drop a .csv file here"
                                       : "Choose a script");
    }
    char count[48];
    std::snprintf(count, sizeof count, "%zu %s   Ctrl+F", scripts_.size(),
                  scripts_.size() == 1 ? "script" : "scripts");
    const float count_width = ImGui::CalcTextSize(count).x;
    list->AddText(ImVec2(p1.x - px(40) - count_width, text_y),
                  ImGui::GetColorU32(theme::text_faint), count);
    ui::draw_icon(list, ui::Icon::chevron_down, ImVec2(p1.x - px(20), p0.y + height * 0.5f),
                  px(20), ImGui::GetColorU32(theme::text_dim));
    ImGui::EndDisabled();

    ImGui::SetNextWindowPos(ImVec2(p0.x, p1.y + px(4)));
    draw_script_picker(width);

    if (!script_errors_.empty()) {
        gap(2);
        constexpr size_t shown = 6;
        for (size_t index = 0; index < script_errors_.size() && index < shown; ++index)
            small_colored(theme::danger, script_errors_[index]);
        if (script_errors_.size() > shown)
            small_colored(theme::danger, "...and " + std::to_string(script_errors_.size() - shown) +
                                             " more (see the Activity log for the first).");
    }
    if (script_) {
        gap(2);
        char summary[200];
        std::snprintf(summary, sizeof summary,
                      "%zu rows (%zu once)  ·  first lap %s  ·  repeat lap %s  ·  uses the %s",
                      script_->size(), timeline_.once_rows(),
                      format_time(timeline_.duration(aoap::Lap::first)).c_str(),
                      format_time(timeline_.duration(aoap::Lap::repeat)).c_str(),
                      aoap::describe_profiles(script_->required_profiles()).c_str());
        small_dim(summary);

        for (const std::string& warning : script_warnings_)
            small_colored(theme::warning, warning);

        const uint32_t available = engine_.connected() ? engine_.connected_profiles()
                                                       : aoap::enabled_profiles(build_setup());
        const uint32_t missing = script_->required_profiles() & ~available;
        if (missing != 0U) {
            small_colored(theme::warning, "Rows for the " + aoap::describe_profiles(missing) +
                                              " are skipped unless that profile is connected.");
        }
    }
    ui::end_card();
}

void App::draw_script_picker(const float width) {
    ImGui::SetNextWindowSize(ImVec2(width, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(8), px(8)));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::surface_hi);
    ImGui::PushStyleColor(ImGuiCol_Border, theme::border_strong);
    const bool open = ImGui::BeginPopup("##picker", ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    if (!open)
        return;

    // Filter first so the keys below act on what is shown.
    const std::vector<std::string> terms = search_terms(script_filter_);
    std::vector<int> shown;
    shown.reserve(scripts_.size());
    for (size_t index = 0; index < scripts_.size(); ++index) {
        bool match = true;
        for (const std::string& term : terms)
            match = match && scripts_[index].lower.find(term) != std::string::npos;
        if (match)
            shown.push_back(static_cast<int>(index));
    }

    // Escape cancels a pending delete, clears text being typed (the field
    // does that itself), and otherwise closes the picker.
    const bool escape = ui::escape_pressed();
    const bool field_clears = picker_typing_ && !script_filter_.empty();
    const std::string before = script_filter_;
    const bool entered = ui::search_box("##search", &script_filter_,
                                        ImGui::GetContentRegionAvail().x, "Search scripts",
                                        focus_search_, &picker_typing_);
    focus_search_ = false;
    if (script_filter_ != before) {
        picker_cursor_ = 0;
        picker_follow_ = true;
        pending_delete_.clear();
    }

    // The cursor is an index into `shown`.
    int cursor = -1;
    if (!shown.empty()) {
        if (script_filter_ == before) {
            // Opening puts the cursor on the loaded script; keep it in range.
            auto found = std::find(shown.begin(), shown.end(), picker_cursor_);
            cursor = found != shown.end() ? static_cast<int>(found - shown.begin()) : 0;
        } else {
            cursor = 0;
        }
    }
    if (cursor >= 0 && pending_delete_.empty()) {
        const int last = static_cast<int>(shown.size()) - 1;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            cursor = std::min(cursor + 1, last);
            picker_follow_ = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            cursor = std::max(cursor - 1, 0);
            picker_follow_ = true;
        }
        picker_cursor_ = shown[static_cast<size_t>(cursor)];
    }
    if (escape) {
        if (!pending_delete_.empty())
            pending_delete_.clear();
        else if (!field_clears)
            ImGui::CloseCurrentPopup();
    }

    std::string choose;
    std::string remove; // deleted after the list is drawn
    if (entered && cursor >= 0 && pending_delete_.empty())
        choose = scripts_[static_cast<size_t>(picker_cursor_)].path;

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
        ImGui::TextDisabled("%s", scripts_.empty() ? "The csv folder is empty."
                                                   : "No script matches this search.");
    }
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    const bool mouse_moved = delta.x != 0.0f || delta.y != 0.0f;
    for (size_t position = 0; position < shown.size(); ++position) {
        const ScriptFile& file = scripts_[static_cast<size_t>(shown[position])];
        const bool loaded = file.path == script_path_;
        const bool deleting = pending_delete_ == file.path;
        ImGui::PushID(file.path.c_str());
        const ImVec2 r0 = ImGui::GetCursorScreenPos();
        const float row_width = ImGui::GetContentRegionAvail().x;
        const ImVec2 r1(r0.x + row_width, r0.y + row);
        const bool mouse_here =
            ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(r0, r1, false);
        if (mouse_here && mouse_moved)
            picker_cursor_ = shown[position];
        const bool current = picker_cursor_ == shown[position];

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
            const std::string question = "Delete " + file.name + ".csv?";
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
            const std::string meta = loaded ? "Loaded  \xC2\xB7  " + file.meta : file.meta;
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
                pending_delete_.clear();
            ImGui::SameLine(0, px(6));
            if (ui::button("Delete", ImVec2(confirm_w, 0), ui::Tone::danger)) {
                remove = file.path;
                pending_delete_.clear();
            }
        } else if (current) {
            ImGui::SetCursorScreenPos(ImVec2(r1.x - button - px(8), r0.y + (row - button) * 0.5f));
            if (ui::icon_button("##delete", ui::Icon::trash, button, ui::Tone::quiet,
                                "Delete this file"))
                pending_delete_ = file.path;
        }

        ImGui::SetCursorScreenPos(r0);
        const bool pressed = ImGui::InvisibleButton("##row", ImVec2(row_width, row));
        if (static_cast<int>(position) == cursor && picker_follow_) {
            ImGui::SetScrollHereY(0.5f);
            picker_follow_ = false;
        }
        if (pressed && !deleting && pending_delete_.empty())
            choose = file.path;
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (!remove.empty())
        delete_script(remove);

    // Anything outside the csv folder can still be opened by path.
    thin_rule(2.0f, 4.0f);
    const float open_width = px(76);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - open_width -
                            ImGui::GetStyle().ItemSpacing.x);
    const bool path_entered =
        ImGui::InputTextWithHint("##path", "Open another file by path (or drop it on the window)",
                                 &path_input_, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ui::button("Open", ImVec2(open_width, 0)) || path_entered) && !path_input_.empty())
        choose = path_input_;
    ImGui::PushFont(nullptr, theme::font_small);
    ImGui::TextDisabled("Up/Down to move, Enter to open, Esc to close");
    ImGui::PopFont();

    if (!choose.empty()) {
        load_script(choose);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

int App::player_key(const PlayerAction action) const noexcept {
    static constexpr int defaults[] = {GLFW_KEY_SPACE, GLFW_KEY_ESCAPE, GLFW_KEY_HOME};
    const int index = static_cast<int>(action);
    return player_keys_[index] != 0 ? player_keys_[index] : defaults[index];
}

std::string App::player_key_conflict(const PlayerAction action, const int key) const {
    static const char* const names[] = {"Play/Pause", "Stop", "Back to start"};
    for (int other = 0; other < 3; ++other) {
        if (other != static_cast<int>(action) && player_key(static_cast<PlayerAction>(other)) == key)
            return std::string(glfw_key_name(key)) + " is already used by " + names[other] + ".";
    }
    // Back to start also answers to Backspace until it is remapped.
    if (action != PlayerAction::restart && player_keys_[2] == 0 && key == GLFW_KEY_BACKSPACE)
        return "Backspace is already used by Back to start.";
    if (is_release_key(key, live_release_key_))
        return std::string(glfw_key_name(key)) + " is the Live mouse release key.";
    return {};
}

void App::draw_player_key_setting(const PlayerAction action, const char* const title) {
    ImGui::PushID(static_cast<int>(action));
    ui::caption(title);
    int& configured = player_keys_[static_cast<int>(action)];
    if (player_key_picking_ == action) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(theme::accent_text));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Press a key...");
        ImGui::PopStyleColor();
        ImGui::SameLine(0, px(10));
        if (ui::button("Cancel", ImVec2(px(70), 0)))
            player_key_picking_ = PlayerAction::none;
        if (!player_key_conflict_.empty())
            small_colored(theme::warning, player_key_conflict_);
        else
            small_dim("Esc cancels.");
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(glfw_key_name(player_key(action)));
        ImGui::SameLine(0, px(10));
        if (ui::button("Set...", ImVec2(px(64), 0))) {
            player_key_picking_ = action;
            player_key_conflict_.clear();
        }
        if (configured != 0) {
            ImGui::SameLine(0, px(6));
            if (ui::button("Reset", ImVec2(px(56), 0)))
                configured = 0;
        }
    }
    ImGui::PopID();
}

void App::draw_transport_card() {
    ui::begin_card("##transport");
    const Phase phase = engine_.phase();
    const aoap::PlaybackStatus status = engine_.player().status();
    const bool live = phase == Phase::playing && status.state != aoap::PlaybackState::stopped;
    const aoap::PlaybackPosition position = live ? status.position : cursor_;
    const bool have_script = script_ != nullptr;
    const int64_t first_ns = have_script ? timeline_.duration(aoap::Lap::first) : 0;
    const int64_t repeat_ns = have_script ? timeline_.duration(aoap::Lap::repeat) : 0;
    const bool has_repeat = have_script && timeline_.rows(aoap::Lap::repeat) > 0;
    // Two laps are drawn only when they differ; otherwise the bar is one lap.
    const bool two_laps = has_repeat && timeline_.once_rows() > 0;

    // Time readout and loop counter.
    const float top = ImGui::GetCursorPosY();
    ImGui::PushFont(nullptr, theme::font_display);
    const float big_line = ImGui::GetTextLineHeight();
    ImGui::TextUnformatted(format_time(position.time_ns).c_str());
    ImGui::PopFont();
    ImGui::SameLine(0, px(10));
    ImGui::SetCursorPosY(top + big_line - ImGui::GetTextLineHeight() - px(4));
    const int64_t lap_ns = position.lap == aoap::Lap::first ? first_ns : repeat_ns;
    ImGui::TextDisabled("/ %s", format_time(lap_ns).c_str());

    std::string where;
    if (!have_script)
        where = "No script";
    else if (!has_repeat)
        where = "Once";
    else {
        const uint64_t current = (live ? status.loops : 0) + 1;
        where = "Loop " + format_count(current);
        if (loop_limit_ > 0)
            where += " of " + format_count(static_cast<uint64_t>(loop_limit_));
    }
    const std::string reports = format_count(live ? status.reports : 0) + " reports";
    const float right = std::max(ImGui::CalcTextSize(where.c_str()).x,
                                 ImGui::CalcTextSize(reports.c_str()).x);
    ImGui::SameLine();
    ImGui::SetCursorPosY(top + px(2));
    ui::align_right(right);
    ImGui::BeginGroup();
    ImGui::TextUnformatted(where.c_str());
    ImGui::TextDisabled("%s", reports.c_str());
    ImGui::EndGroup();
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), top + big_line + px(4)));

    // Timeline: the first lap (once-only rows purple, repeated rows green in
    // the order written) and the repeat lap (green) as one continuous bar, so
    // a single drag can seek across the whole cycle.
    const float width = ImGui::GetContentRegionAvail().x;
    const float length_w = ImGui::CalcTextSize("00:00.000").x;
    const float bar_w = std::max(width - length_w - px(20), px(60));
    const int64_t total_ns = two_laps ? first_ns + repeat_ns : first_ns;
    const double inverse_total = total_ns > 0 ? 1.0 / static_cast<double>(total_ns) : 0.0;
    const int64_t lap_offset = (two_laps && position.lap == aoap::Lap::repeat) ? first_ns : 0;
    const float position_fraction =
        have_script ? static_cast<float>(static_cast<double>(lap_offset + position.time_ns) *
                                         inverse_total)
                    : 0.0f;
    const float boundary_fraction =
        two_laps ? static_cast<float>(static_cast<double>(first_ns) * inverse_total) : 1.0f;

    const float row_top = ImGui::GetCursorPosY();
    const float bar_h = px(9);
    const float bar_row = std::max(bar_h + px(14), px(20)); // matches scrub_bar's own row calc
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!have_script);
    ImGui::InvisibleButton("##timeline", ImVec2(bar_w, bar_row));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool released = ImGui::IsItemDeactivated();
    ImGui::EndDisabled();
    const float mouse_fraction = std::clamp((ImGui::GetIO().MousePos.x - p0.x) / bar_w, 0.0f, 1.0f);
    if (active)
        scrub_timeline_ = mouse_fraction;
    const float shown_fraction =
        std::clamp(active ? scrub_timeline_ : position_fraction, 0.0f, 1.0f);

    ImDrawList* list = ImGui::GetWindowDrawList();
    const float cy = std::round(p0.y + bar_row * 0.5f);
    const float radius = bar_h * 0.5f;
    const ImVec2 t0(p0.x, cy - radius);
    const ImVec2 t1(p0.x + bar_w, cy + radius);
    list->AddRectFilled(t0, t1, ImGui::GetColorU32(theme::field_active), radius);

    // Rounded only at the ends of the whole bar, so neighbouring spans join.
    const auto fill_span = [&](const float x0, float x1, const ImU32 color) {
        if (x1 - x0 < 0.5f)
            return;
        ImDrawFlags flags = ImDrawFlags_RoundCornersNone;
        if (x0 <= t0.x + 0.5f) {
            flags |= ImDrawFlags_RoundCornersLeft;
            x1 = std::max(x1, t0.x + bar_h);
        }
        if (x1 >= t1.x - 0.5f)
            flags |= ImDrawFlags_RoundCornersRight;
        list->AddRectFilled(ImVec2(x0, t0.y), ImVec2(x1, t1.y), color, radius, flags);
    };
    // Spans not reached yet show their colour faintly, so the once-only
    // parts are visible before they play.
    const auto faint = [](const ImU32 color) {
        const ImU32 alpha = ((color >> IM_COL32_A_SHIFT) & 0xFFU) * 70U / 255U;
        return ImGui::GetColorU32((color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT));
    };
    const auto x_at = [&](const int64_t ns) {
        return p0.x + static_cast<float>(static_cast<double>(ns) * inverse_total) * bar_w;
    };
    const float filled_x = p0.x + bar_w * shown_fraction;
    for (const aoap::Timeline::Run& run : timeline_.first_runs) {
        const ImU32 tone = run.once ? theme::accent : theme::success;
        fill_span(x_at(run.start), x_at(run.end), faint(tone));
        fill_span(x_at(run.start), std::min(x_at(run.end), filled_x), ImGui::GetColorU32(tone));
    }
    if (two_laps) {
        const float boundary_x = p0.x + bar_w * boundary_fraction;
        fill_span(boundary_x, t1.x, faint(theme::success));
        fill_span(boundary_x, filled_x, ImGui::GetColorU32(theme::success));
        list->AddRectFilled(ImVec2(boundary_x - px(0.75f), t0.y),
                            ImVec2(boundary_x + px(0.75f), t1.y),
                            ImGui::GetColorU32(theme::background));
    }
    if (hovered && !active && have_script) {
        const float x = p0.x + bar_w * mouse_fraction;
        list->AddLine(ImVec2(x, cy - bar_h * 1.6f), ImVec2(x, cy + bar_h * 1.6f),
                      ImGui::GetColorU32(theme::text_dim), px(1));
        ImGui::SetTooltip(
            "%s", format_time(static_cast<int64_t>(static_cast<double>(total_ns) * mouse_fraction))
                      .c_str());
    } else if (active) {
        ImGui::SetTooltip("%s", format_time(static_cast<int64_t>(
                                    static_cast<double>(total_ns) * scrub_timeline_))
                                    .c_str());
    }
    const float knob_x = p0.x + bar_w * shown_fraction;
    const float knob_r = (hovered || active) ? bar_h * 1.5f : bar_h * 1.2f;
    list->AddCircleFilled(ImVec2(knob_x, cy), knob_r, IM_COL32(245, 245, 248, 255), 24);

    ImGui::SameLine(0, px(20));
    ImGui::SetCursorPosY(row_top + (bar_row - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::TextDisabled("%s", format_time(total_ns).c_str());
    ImGui::SetCursorPosY(row_top + bar_row + px(2));

    if (released && have_script) {
        const int64_t absolute_ns =
            static_cast<int64_t>(static_cast<double>(total_ns) * scrub_timeline_);
        if (two_laps && absolute_ns > first_ns)
            seek({aoap::Lap::repeat, std::clamp<int64_t>(absolute_ns - first_ns, 0, repeat_ns)});
        else // one lap is drawn: stay in the lap that is playing so the loop count is kept
            seek({two_laps ? aoap::Lap::first : position.lap, absolute_ns});
    }

    const auto key_hint = [this](const PlayerAction action) -> std::string {
        const int configured = player_keys_[static_cast<int>(action)];
        if (configured != 0)
            return glfw_key_name(configured);
        return action == PlayerAction::play   ? "Space"
               : action == PlayerAction::stop ? "Esc or Ctrl+S"
                                              : "Home or Backspace";
    };

    // Transport buttons, centred.
    gap(2);
    const float small = px(38);
    const float large = px(52);
    const float spacing = px(18);
    const float total = small * 2 + large + spacing * 2;
    const float row_y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         (ImGui::GetContentRegionAvail().x - total) * 0.5f);
    ImGui::SetCursorPosY(row_y + (large - small) * 0.5f);
    ImGui::BeginDisabled(!have_script);
    if (ui::icon_button("##restart", ui::Icon::restart, small, ui::Tone::secondary,
                        ("Back to the start (" + key_hint(PlayerAction::restart) + ")").c_str()))
        seek({});
    ImGui::EndDisabled();

    ImGui::SameLine(0, spacing);
    ImGui::SetCursorPosY(row_y);
    const bool can_start = phase == Phase::connected && have_script;
    const bool can_toggle = phase == Phase::playing && status.state != aoap::PlaybackState::stopped;
    const bool pausing = can_toggle && status.state == aoap::PlaybackState::playing;
    ImGui::BeginDisabled(!can_start && !can_toggle);
    if (ui::icon_button("##play", pausing ? ui::Icon::pause : ui::Icon::play, large,
                        ui::Tone::primary,
                        ((pausing ? "Pause (" : "Play (") + key_hint(PlayerAction::play) + ")")
                            .c_str()))
        toggle_playback();
    ImGui::EndDisabled();
    if (!can_start && !can_toggle && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(!engine_.connected() ? "Connect a device first"
                                               : "Choose a script first");

    ImGui::SameLine(0, spacing);
    ImGui::SetCursorPosY(row_y + (large - small) * 0.5f);
    ImGui::BeginDisabled(phase != Phase::playing);
    if (ui::icon_button("##stop", ui::Icon::stop, small, ui::Tone::secondary,
                        ("Stop and release everything (" + key_hint(PlayerAction::stop) + ")").c_str()))
        stop_playback();
    ImGui::EndDisabled();
    ImGui::SetCursorPosY(row_y + large);
    thin_rule(2.0f, 2.0f);

    // Active touches: every contact currently down, including Live's own
    // reserved finger (see live_finger()), since playback and Live control
    // can now run at once.
    if (engine_.connected_setup().touch.enabled && !touch_contacts_active_.empty()) {
        gap(1);
        ImGui::PushFont(nullptr, theme::font_small);
        draw_touch_contacts();
        ImGui::PopFont();
        gap(1);
        thin_rule(2.0f, 2.0f);
    }

    // Speed, loop limit, live offset.
    if (ImGui::BeginTable("##settings", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ui::caption("Speed");
        // Typed in directly; the value takes effect as soon as it parses.
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputFloat("##speed", &speed_, 0.0f, 0.0f, "%.2fx",
                              ImGuiInputTextFlags_CharsScientific)) {
            speed_ = std::clamp(speed_, static_cast<float>(aoap::Player::min_speed),
                                static_cast<float>(aoap::Player::max_speed));
            engine_.player().set_speed(speed_);
        }
        static constexpr float presets[] = {0.5f, 1.0f, 2.0f, 4.0f};
        static const char* const preset_labels[] = {"0.5x", "1x", "2x", "4x"};
        const float preset_width = (ImGui::GetContentRegionAvail().x - px(6) * 3) / 4.0f;
        for (int index = 0; index < 4; ++index) {
            if (index > 0)
                ImGui::SameLine(0, px(6));
            if (ui::button(preset_labels[index], ImVec2(preset_width, 0))) {
                speed_ = presets[index];
                engine_.player().set_speed(speed_);
            }
        }

        ImGui::TableNextColumn();
        ui::caption("Loops");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputInt("##loops", &loop_limit_, 1, 10)) {
            loop_limit_ = std::max(loop_limit_, 0);
            engine_.player().set_loop_limit(loop_limit_);
        }
        small_dim(loop_limit_ == 0 ? "Repeats until stopped" : "Stops after this many loops");

        ImGui::TableNextColumn();
        ui::caption("Offset");
        // The offset belongs to the run in progress: it starts at zero and
        // can only be nudged while something is playing.
        const bool live_offset = engine_.phase() == Phase::playing;
        const double offset_ms = static_cast<double>(engine_.player().offset_ns()) / 1e6;
        ImGui::BeginDisabled(!live_offset);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%+.1f ms", offset_ms);
        ImGui::SameLine();
        ui::align_right(px(60));
        if (ui::button("Reset", ImVec2(px(60), 0)))
            engine_.player().set_offset_ns(0);
        static constexpr int steps[] = {-10, -1, 1, 10};
        const float step_width = (ImGui::GetContentRegionAvail().x - px(6) * 3) / 4.0f;
        for (int index = 0; index < 4; ++index) {
            if (index > 0)
                ImGui::SameLine(0, px(6));
            char label[16];
            std::snprintf(label, sizeof label, "%+d", steps[index]);
            if (ui::button(label, ImVec2(step_width, 0)))
                engine_.player().adjust_offset_ns(static_cast<int64_t>(steps[index]) * ns_per_ms);
        }
        ImGui::EndDisabled();
        if (!live_offset && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Available while playing; it resets at every start.");
        else if (live_offset && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Ctrl+Left/Right also nudges this by 1 ms.");
        ImGui::EndTable();
    }

    gap(4);
    if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        draw_player_key_setting(PlayerAction::play, "Play / Pause key");
        ImGui::TableNextColumn();
        draw_player_key_setting(PlayerAction::stop, "Stop key");
        ImGui::TableNextColumn();
        draw_player_key_setting(PlayerAction::restart, "Back to start key");
        ImGui::EndTable();
    }
    ui::end_card();
}

} // namespace gui
