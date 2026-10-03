// SPDX-License-Identifier: MIT
// The Settings tab: how the window looks (theme, glass, background) and how
// the Live preview looks (the colour around the phone, the reference image).
#include "app.hpp"

#include "app_common.hpp"
#include "backdrop.hpp"
#include "theme.hpp"

#include "aoahid_player/paths.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <nfd.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>
#include <vector>

namespace gui {
using namespace detail;

namespace {

// A row of buttons of which exactly one is on. Returns the chosen index.
int segmented(const char* id, const char* const* labels, const int count, const int current) {
    ImGui::PushID(id);
    const float spacing = px(6);
    const float width =
        (ImGui::GetContentRegionAvail().x - spacing * static_cast<float>(count - 1)) /
        static_cast<float>(count);
    int chosen = current;
    for (int index = 0; index < count; ++index) {
        if (index != 0)
            ImGui::SameLine(0, spacing);
        ImGui::PushID(index);
        if (ui::button(labels[index], ImVec2(width, 0),
                       index == current ? ui::Tone::primary : ui::Tone::secondary))
            chosen = index;
        ImGui::PopID();
    }
    ImGui::PopID();
    return chosen;
}

// A 0xRRGGBB colour as a swatch and hex field, with ImGui's picker on click.
bool color_edit(const char* id, int* rgb) {
    float color[3] = {static_cast<float>((*rgb >> 16) & 0xFF) / 255.0f,
                      static_cast<float>((*rgb >> 8) & 0xFF) / 255.0f,
                      static_cast<float>(*rgb & 0xFF) / 255.0f};
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (!ImGui::ColorEdit3(id, color, ImGuiColorEditFlags_DisplayHex))
        return false;
    const auto channel = [](const float value) {
        return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    *rgb = channel(color[0]) << 16 | channel(color[1]) << 8 | channel(color[2]);
    return true;
}

// NFD's last error, which it says to clear once read.
std::string picker_error() {
    const char* text = NFD_GetError();
    std::string error = text != nullptr ? text : "unknown error";
    NFD_ClearError();
    return error;
}

// The system's own file picker for one image, starting in `beside`'s folder
// when there is one. It blocks this thread until closed; the engine keeps
// running on its own. Returns the chosen path, or empty when cancelled or
// when the picker failed (`error` then says why).
std::string choose_image(const std::string& beside, std::string& error) {
    if (NFD_Init() != NFD_OKAY) {
        error = picker_error();
        return {};
    }
    const nfdu8filteritem_t filter{"Images", "png,jpg,jpeg,bmp"};
    std::string folder;
    if (!beside.empty())
        folder = aoap::path_utf8(aoap::utf8_path(beside).parent_path());
    nfdu8char_t* chosen = nullptr;
    const nfdresult_t result =
        NFD_OpenDialogU8(&chosen, &filter, 1, folder.empty() ? nullptr : folder.c_str());
    std::string path;
    if (result == NFD_OKAY) {
        path = chosen;
        NFD_FreePathU8(chosen);
    } else if (result == NFD_ERROR) {
        error = picker_error();
    }
    NFD_Quit();
    return path;
}

// The accent swatches: the default light purple first.
constexpr int accent_swatches[] = {0xB4A5FF, 0x7CB7FF, 0x4FD1C5, 0x7FDCA4,
                                   0xE8D36A, 0xFFB38A, 0xF58FB5, 0xE8E8EE};

// A round colour swatch; a ring marks the chosen one. True when clicked.
bool swatch(const int color, const bool chosen) {
    const float size = px(24);
    ImGui::PushID(color);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##swatch", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 centre(p0.x + size * 0.5f, p0.y + size * 0.5f);
    list->AddCircleFilled(centre, size * 0.5f - px(4), theme::rgb(static_cast<unsigned>(color)),
                          24);
    if (chosen || hovered)
        list->AddCircle(centre, size * 0.5f - px(1),
                        ImGui::GetColorU32(chosen ? theme::text : theme::text_faint), 24, px(1.5f));
    return pressed;
}

constexpr const char* ui_scale_labels[] = {"Small", "Normal", "Large", "Larger"};

constexpr double toast_fade_seconds = 0.15;

} // namespace

ImU32 App::background_color() const {
    return bg_mode_ == 1 ? theme::rgb(static_cast<unsigned>(bg_color_)) : theme::background;
}

void App::load_background(const std::string& path) {
    const std::string error = backdrop::load(path);
    if (!error.empty()) {
        log_.message(aoap::Severity::warning, "Background: " + error);
        return;
    }
    bg_image_ = path;
    bg_image_input_ = path;
    bg_mode_ = 2;
}

void App::apply_look(const Settings& settings) {
    dark_theme_ = settings.dark_theme;
    accent_ = settings.accent;
    glass_ = settings.glass;
    gloss_ = settings.gloss;
    rim_ = settings.rim;
    card_rounding_ = settings.card_rounding;
    motion_ = settings.motion;
    ui_scale_ = nearest_ui_scale(settings.ui_scale);
    theme::set_mode(dark_theme_);
    theme::set_accent(static_cast<unsigned>(accent_));
    theme::set_glass(glass_);
    theme::set_shine(gloss_, rim_);
    theme::card_rounding = static_cast<float>(card_rounding_);
    theme_dirty_ = true;

    bg_mode_ = settings.bg_mode;
    bg_color_ = settings.bg_color;
    bg_blur_ = settings.bg_blur;
    bg_dim_ = settings.bg_dim;
    backdrop::set_blur(bg_blur_);
    if (settings.bg_image != bg_image_) {
        bg_image_ = settings.bg_image;
        bg_image_input_ = settings.bg_image;
        // The old picture goes first: if the new one cannot be loaded, the
        // theme's colour shows, and the path stays so the next start tries
        // it again (a drive not mounted yet).
        backdrop::clear();
        if (!bg_image_.empty()) {
            const std::string error = backdrop::load(bg_image_);
            if (!error.empty())
                log_.message(aoap::Severity::warning, "Background: " + error);
        }
    }

    live_phone_glass_ = settings.live_phone_glass;
    live_phone_clear_ = settings.live_phone_clear;
    live_phone_rounding_ = settings.live_phone_rounding;
    sidebar_right_ = settings.sidebar_right;
    log_hidden_ = settings.log_hidden;
    toasts_enabled_ = settings.toasts;
    if (!toasts_enabled_)
        toasts_.clear();
}

void App::refresh_themes() {
    theme_names_ = list_themes();
    themes_stale_ = false;
    if (std::find(theme_names_.begin(), theme_names_.end(), theme_selected_) ==
        theme_names_.end())
        theme_selected_.clear();
}

void App::push_toast(const aoap::Severity severity, std::string text) {
    if (!toasts_enabled_)
        return;
    const double now = ImGui::GetTime();
    const double life = severity == aoap::Severity::error     ? 8.0
                        : severity == aoap::Severity::warning ? 5.0
                                                              : 3.0;
    toasts_.push_back(Toast{toast_next_id_++, severity, std::move(text), now, now + life, false});
    while (toasts_.size() > 3)
        toasts_.pop_front();
}

void App::collect_toasts() {
    const uint64_t version = log_.version();
    if (version == toast_log_seen_)
        return;
    if (!toasts_enabled_) {
        toast_log_seen_ = version;
        return;
    }
    // What arrived since last time, read under one lock with the version it
    // matches; of that, the warnings and errors become notices. (Clearing
    // the log also bumps the version; it adds nothing.)
    std::vector<std::pair<aoap::Severity, std::string>> problems;
    toast_log_seen_ = log_.visit_since(toast_log_seen_, [&](const ActivityLog::Entry& entry) {
        if (entry.severity != aoap::Severity::info)
            problems.emplace_back(entry.severity, entry.text);
    });
    for (auto& [severity, text] : problems)
        push_toast(severity, std::move(text));
}

// Newest at the bottom, in the bottom corner away from the sidebar (so they
// never cover Connect), over everything, full screen included. Nearly
// solid, so text under them does not show through. Hovering keeps one up;
// a click dismisses it.
void App::draw_toasts() {
    const double now = ImGui::GetTime();
    std::erase_if(toasts_, [&](const Toast& toast) { return toast.hide_at <= now; });
    if (toasts_.empty())
        return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float margin = px(20);
    const bool left = sidebar_right_ && !live_fullscreen_;
    ImGui::SetNextWindowPos(
        ImVec2(left ? viewport->WorkPos.x + margin
                    : viewport->WorkPos.x + viewport->WorkSize.x - margin,
               viewport->WorkPos.y + viewport->WorkSize.y - margin),
        ImGuiCond_Always, ImVec2(left ? 0.0f : 1.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::Begin("##toasts", nullptr, flags);
    ImGui::PopStyleVar();
    for (Toast& toast : toasts_) {
        const auto fade = static_cast<float>(std::clamp(
            std::min(now - toast.shown_at, toast.hide_at - now) / toast_fade_seconds, 0.0, 1.0));
        ImGui::PushID(static_cast<int>(toast.id));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade);
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(18), px(12)));
        ImGui::BeginChild("##toast", ImVec2(px(360), 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding |
                              ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 p1(p0.x + ImGui::GetWindowWidth(), p0.y + ImGui::GetWindowHeight());
        ImDrawList* list = ImGui::GetWindowDrawList();
        list->PushClipRect(p0, p1);
        ui::paint_glass(list, p0, p1, ImGui::GetStyle().ChildRounding, theme::glass_fill(0.08f));
        list->PopClipRect();
        // A stripe on the left edge says what kind of notice it is.
        const ImU32 kind = toast.severity == aoap::Severity::error     ? theme::danger
                           : toast.severity == aoap::Severity::warning ? theme::warning
                                                                       : theme::accent;
        const float inset = ImGui::GetStyle().ChildRounding * 0.5f + px(4);
        list->AddRectFilled(
            ImVec2(p0.x + px(6), p0.y + inset),
            ImVec2(p0.x + px(9), p0.y + ImGui::GetWindowHeight() - inset),
            ImGui::GetColorU32(kind), px(2));
        ImGui::TextWrapped("%s", toast.text.c_str());
        ImGui::EndChild();
        if (ImGui::IsItemClicked()) {
            toast.dismissed = true;
            toast.hide_at = std::min(toast.hide_at, now + toast_fade_seconds);
        } else if (ImGui::IsItemHovered() && !toast.dismissed) {
            toast.hide_at = std::max(toast.hide_at, now + 1.0);
        }
        ImGui::PopStyleVar();
        ImGui::PopID();
    }
    ImGui::End();
}

void App::draw_settings() {
    if (themes_stale_)
        refresh_themes();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const auto left_cards = [&] {
        draw_presets_card();
        gap(2);
        draw_appearance_card();
        gap(2);
        draw_glass_card();
    };
    const auto right_cards = [&] {
        draw_background_card();
        gap(2);
        draw_live_look_card();
        gap(2);
        draw_layout_card();
    };
    if (available.x >= px(760)) {
        const float spacing = px(10);
        const float left = std::floor((available.x - spacing) * 0.5f);
        ImGui::BeginChild("##settings_left", ImVec2(left, available.y), ImGuiChildFlags_None);
        left_cards();
        ImGui::EndChild();
        ImGui::SameLine(0, spacing);
        ImGui::BeginChild("##settings_right", ImVec2(0, available.y), ImGuiChildFlags_None);
        right_cards();
        ImGui::EndChild();
    } else {
        ImGui::BeginChild("##settings", ImVec2(0, available.y), ImGuiChildFlags_None);
        left_cards();
        gap(2);
        right_cards();
        ImGui::EndChild();
    }
}

void App::draw_presets_card() {
    ui::begin_card("##presets", "Presets");
    if (!theme_current_.empty()) {
        ImGui::SameLine();
        ImGui::PushFont(nullptr, theme::font_small);
        const float width = ImGui::CalcTextSize(theme_current_.c_str()).x;
        ui::align_right(width);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(theme::text_faint), "%s", theme_current_.c_str());
        ImGui::PopFont();
    }
    const auto apply = [&](const std::string& name) {
        Settings look = current_settings();
        std::string error;
        if (!load_theme(name, look, error)) {
            log_.message(aoap::Severity::warning, "Preset: " + error + ".");
            return;
        }
        // A preset may come from someone else. Its background image is not
        // opened when it is on a network path (a UNC path on Windows connects
        // to that server just by being opened); the current background stays.
        if (look.bg_image != bg_image_ &&
            (look.bg_image.rfind("\\\\", 0) == 0 || look.bg_image.rfind("//", 0) == 0)) {
            log_.message(aoap::Severity::warning,
                         "Preset: the background image is on a network path and was not "
                         "loaded.");
            look.bg_image = bg_image_;
            look.bg_mode = bg_mode_;
        }
        apply_look(look);
        theme_current_ = name;
    };

    if (theme_names_.empty()) {
        small_dim("Name the current look below and save it; it shows up here.");
    } else {
        const float row = ImGui::GetTextLineHeightWithSpacing();
        const float rows = static_cast<float>(std::min<size_t>(theme_names_.size(), 5));
        ImGui::BeginChild("##preset_list", ImVec2(0, row * rows + px(4)), ImGuiChildFlags_None);
        for (const std::string& name : theme_names_) {
            if (ImGui::Selectable(name.c_str(), name == theme_selected_,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                theme_selected_ = name;
                theme_pending_delete_.clear();
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    apply(name);
            }
        }
        ImGui::EndChild();
        ImGui::BeginDisabled(theme_selected_.empty());
        if (theme_pending_delete_.empty()) {
            if (ui::button("Apply", ImVec2(px(80), 0), ui::Tone::primary))
                apply(theme_selected_);
            ImGui::SameLine(0, px(6));
            if (ui::button("Delete", ImVec2(px(80), 0), ui::Tone::danger))
                theme_pending_delete_ = theme_selected_;
        } else {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Delete \"%s\"?", theme_pending_delete_.c_str());
            ImGui::SameLine(0, px(8));
            if (ui::button("Yes", ImVec2(px(56), 0), ui::Tone::danger)) {
                std::string error;
                if (!delete_theme(theme_pending_delete_, error))
                    log_.message(aoap::Severity::warning, "Preset: " + error + ".");
                if (theme_current_ == theme_pending_delete_)
                    theme_current_.clear();
                theme_pending_delete_.clear();
                themes_stale_ = true;
            }
            ImGui::SameLine(0, px(6));
            if (ui::button("No", ImVec2(px(56), 0)))
                theme_pending_delete_.clear();
        }
        ImGui::EndDisabled();
    }

    thin_rule(6.0f, 6.0f);
    const float button = px(96);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button -
                            ImGui::GetStyle().ItemSpacing.x);
    const bool entered =
        ImGui::InputTextWithHint("##preset_name", "Name for the current look",
                                 &theme_name_input_, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const std::string problem = theme_name_problem(theme_name_input_);
    const bool exists = std::find(theme_names_.begin(), theme_names_.end(), theme_name_input_) !=
                        theme_names_.end();
    ImGui::BeginDisabled(!problem.empty());
    if ((ui::button(exists ? "Overwrite" : "Save", ImVec2(button, 0)) || entered) &&
        problem.empty()) {
        std::string error;
        if (save_theme(theme_name_input_, current_settings(), error)) {
            theme_current_ = theme_name_input_;
            theme_selected_ = theme_name_input_;
            themes_stale_ = true;
        } else {
            log_.message(aoap::Severity::warning, "Preset: " + error + ".");
        }
    }
    ImGui::EndDisabled();
    if (!theme_name_input_.empty() && !problem.empty())
        small_colored(theme::warning, problem);
    ui::end_card();
}

void App::draw_appearance_card() {
    ui::begin_card("##appearance", "Appearance");
    field("Theme");
    static const char* const themes[] = {"Dark", "Light"};
    const int theme_choice = segmented("##theme", themes, 2, dark_theme_ ? 0 : 1);
    if ((theme_choice == 0) != dark_theme_) {
        dark_theme_ = theme_choice == 0;
        theme::set_mode(dark_theme_);
        theme_dirty_ = true;
    }

    field("Accent");
    bool accent_changed = false;
    for (size_t index = 0; index < std::size(accent_swatches); ++index) {
        if (index != 0)
            ImGui::SameLine(0, px(4));
        if (swatch(accent_swatches[index], accent_ == accent_swatches[index])) {
            accent_ = accent_swatches[index];
            accent_changed = true;
        }
    }
    field("");
    accent_changed |= color_edit("##accent", &accent_);
    if (accent_changed) {
        theme::set_accent(static_cast<unsigned>(accent_));
        theme_dirty_ = true;
    }

    field("Size");
    int size_choice = 1;
    for (size_t index = 0; index < std::size(ui_scales); ++index) {
        if (ui_scales[index] == ui_scale_)
            size_choice = static_cast<int>(index);
    }
    const int chosen_size = segmented("##ui_scale", ui_scale_labels,
                                      static_cast<int>(std::size(ui_scale_labels)), size_choice);
    if (chosen_size != size_choice) {
        ui_scale_ = ui_scales[chosen_size];
        theme_dirty_ = true;
    }

    ui::toggle("Motion when switching tabs", &motion_);
    ui::end_card();
}

void App::draw_glass_card() {
    ui::begin_card("##glass", "Glass");
    field("Glass");
    float percent = glass_ * 100.0f;
    if (ui::slider("##glass", &percent, 0.0f, 70.0f, 0, "%", 1.0f)) {
        glass_ = percent / 100.0f;
        theme::set_glass(glass_);
    }
    field("Gloss");
    float gloss_percent = gloss_ * 100.0f;
    if (ui::slider("##gloss", &gloss_percent, 0.0f, 200.0f, 0, "%", 1.0f)) {
        gloss_ = gloss_percent / 100.0f;
        theme::set_shine(gloss_, rim_);
    }
    field("Rim");
    float rim_percent = rim_ * 100.0f;
    if (ui::slider("##rim", &rim_percent, 0.0f, 200.0f, 0, "%", 1.0f)) {
        rim_ = rim_percent / 100.0f;
        theme::set_shine(gloss_, rim_);
    }
    field("Corners");
    if (ui::slider_int("##card_rounding", &card_rounding_, 0, 24, "px")) {
        theme::card_rounding = static_cast<float>(card_rounding_);
        theme_dirty_ = true;
    }
    small_dim("Glass is how much the cards let the background show through (0% is solid); "
              "Gloss and Rim are the light on their top and edges.");
    ui::end_card();
}

void App::draw_background_card() {
    ui::begin_card("##background", "Background");
    field("Show");
    static const char* const modes[] = {"None", "Color", "Image"};
    bg_mode_ = segmented("##bg_mode", modes, 3, bg_mode_);

    if (bg_mode_ == 0) {
        small_dim("The theme's own background colour.");
    } else if (bg_mode_ == 1) {
        field("Color");
        color_edit("##bg_color", &bg_color_);
    } else {
        if (ui::button("Choose...", ImVec2(px(96), 0), ui::Tone::primary)) {
            std::string error;
            const std::string path = choose_image(bg_image_, error);
            if (!path.empty())
                load_background(path);
            else if (!error.empty())
                log_.message(aoap::Severity::warning, "File picker: " + error);
        }
        ImGui::SameLine(0, px(6));
        ImGui::BeginDisabled(!backdrop::loaded());
        if (ui::button("Clear", ImVec2(px(64), 0))) {
            backdrop::clear();
            bg_image_.clear();
            bg_image_input_.clear();
        }
        ImGui::EndDisabled();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::InputTextWithHint("##bg_image", "Or type a path and press Enter",
                                     &bg_image_input_, ImGuiInputTextFlags_EnterReturnsTrue) &&
            !bg_image_input_.empty())
            load_background(bg_image_input_);
        small_dim(backdrop::loaded()
                      ? "A .png, .jpg, or .bmp. Dropping one on the window (outside the Live tab) "
                        "works too."
                      : "Choose a .png, .jpg, or .bmp, or drop one on the window outside the "
                        "Live tab.");
        gap(2);
        field("Blur");
        if (ui::slider_int("##bg_blur", &bg_blur_, 0, 40, "px"))
            backdrop::set_blur(bg_blur_);
        field("Dim");
        float dim_percent = bg_dim_ * 100.0f;
        if (ui::slider("##bg_dim", &dim_percent, 0.0f, 80.0f, 0, "%", 1.0f))
            bg_dim_ = dim_percent / 100.0f;
        small_dim(dark_theme_ ? "Blur softens the picture behind the glass (0 keeps it sharp); "
                                "Dim darkens all of it so text stays readable."
                              : "Blur softens the picture behind the glass (0 keeps it sharp); "
                                "Dim lightens all of it so text stays readable.");
    }
    ui::end_card();
}

void App::draw_live_look_card() {
    ui::begin_card("##live_look", "Live preview");
    ui::toggle("Glass phone screen", &live_phone_glass_);
    small_dim("Off, the phone's screen is black. Around the phone, the window's background "
              "always shows.");
    if (live_phone_glass_) {
        field("Clear");
        float clear_percent = live_phone_clear_ * 100.0f;
        if (ui::slider("##phone_clear", &clear_percent, 0.0f, 100.0f, 0, "%", 1.0f))
            live_phone_clear_ = clear_percent / 100.0f;
    }
    field("Corners");
    ui::slider_int("##phone_rounding", &live_phone_rounding_, 0, 60, "px");

    thin_rule(6.0f, 6.0f);
    ui::caption("Reference image");
    // An optional picture over the preview (a screenshot works well) to line
    // touches up against. Position, size, rotation, opacity, and the lock
    // persist between runs; the file itself is reloaded at startup if still
    // there. Moving it is done on the preview itself.
    const auto load_reference = [&](const std::string& path) {
        const std::string error = live_image_.load(path);
        if (!error.empty())
            log_.message(aoap::Severity::warning, error);
        else
            live_image_path_input_ = path;
    };
    if (ui::button("Choose...", ImVec2(px(96), 0), ui::Tone::primary)) {
        std::string error;
        const std::string path = choose_image(live_image_.path(), error);
        if (!path.empty())
            load_reference(path);
        else if (!error.empty())
            log_.message(aoap::Severity::warning, "File picker: " + error);
    }
    ImGui::SameLine(0, px(6));
    ImGui::BeginDisabled(!live_image_.loaded());
    if (ui::button("Clear", ImVec2(px(64), 0)))
        live_image_.clear();
    ImGui::SameLine(0, px(12));
    ui::toggle("Lock image", &live_image_.locked);
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputTextWithHint("##live_image_path", "Or type a path and press Enter",
                                 &live_image_path_input_, ImGuiInputTextFlags_EnterReturnsTrue) &&
        !live_image_path_input_.empty())
        load_reference(live_image_path_input_);
    if (live_image_.loaded()) {
        small_dim(live_image_.locked
                      ? "Locked: touches and clicks pass straight through it."
                      : "On the Live preview, drag it to move, its corner handle to resize, its "
                        "top handle to rotate.");
        gap(2);
        field("Opacity");
        float opacity_percent = live_image_.opacity() * 100.0f;
        if (ui::slider("##live_image_opacity", &opacity_percent, 0.0f, 100.0f, 0, "%", 1.0f))
            live_image_.set_opacity(opacity_percent / 100.0f);
        if (ui::button("Reset size/rotation", ImVec2(px(150), 0)))
            live_image_.reset_transform();
    } else {
        small_dim("Choose a screenshot, or drop an image file on the Live preview.");
    }
    ui::end_card();
}

void App::draw_layout_card() {
    ui::begin_card("##layout", "Layout");
    field("Sidebar");
    static const char* const sides[] = {"Left", "Right"};
    sidebar_right_ = segmented("##sidebar_side", sides, 2, sidebar_right_ ? 1 : 0) == 1;
    bool show_log = !log_hidden_;
    if (ui::toggle("Show activity panel", &show_log))
        log_hidden_ = !show_log;
    if (ui::toggle("Notifications", &toasts_enabled_) && !toasts_enabled_)
        toasts_.clear();
    small_dim("Notifications show warnings, errors, and the connection in the bottom-right "
              "corner for a few seconds.");
    ui::end_card();
}

} // namespace gui
