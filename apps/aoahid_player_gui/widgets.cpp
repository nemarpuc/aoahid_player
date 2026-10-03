// SPDX-License-Identifier: MIT
#include "widgets.hpp"

#include "backdrop.hpp"
#include "theme.hpp"

#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace gui::ui {
namespace {

constexpr float pi = 3.14159265358979f;

// Applies the style alpha, so custom drawing fades with BeginDisabled().
ImU32 faded(const ImU32 color) { return ImGui::GetColorU32(color); }

ImU32 with_alpha(const ImU32 color, const unsigned alpha) {
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

// Scales the alpha of the vertices added since `start` from `top` at `y0` to
// `bottom` at `y1` (out of 255). Scaling, not replacing, keeps the zero-alpha
// fringe ImGui adds for anti-aliasing.
void fade_vertical(ImDrawList* list, const int start, const float y0, const float y1,
                   const unsigned top, const unsigned bottom) {
    const float span = std::max(y1 - y0, 1.0f);
    for (int index = start; index < list->VtxBuffer.Size; ++index) {
        ImDrawVert& vertex = list->VtxBuffer[index];
        const float t = std::clamp((vertex.pos.y - y0) / span, 0.0f, 1.0f);
        const float scale = (static_cast<float>(top) +
                             (static_cast<float>(bottom) - static_cast<float>(top)) * t) /
                            255.0f;
        const auto alpha = static_cast<ImU32>(
            static_cast<float>((vertex.col >> IM_COL32_A_SHIFT) & 0xFFU) * scale + 0.5f);
        vertex.col = (vertex.col & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
    }
}

struct Colors {
    ImU32 fill;
    ImU32 fill_hover;
    ImU32 fill_active;
    ImU32 text;
};

Colors colors_for(const Tone tone) {
    switch (tone) {
    case Tone::primary:
        if (theme::glass_enabled()) {
            // Follows the Glass "Clear" setting; hover and press firm up the
            // fill without ever making it solid.
            const float opacity = 0.80f - 0.50f * theme::current_glass();
            const auto alpha = [](const float value) {
                return static_cast<unsigned>(std::lround(255.0f * std::clamp(value, 0.0f, 0.9f)));
            };
            // Over a dark window the fill ends up darker than the accent,
            // so light text reads better than the accent's own ink.
            return {with_alpha(theme::accent, alpha(opacity)),
                    with_alpha(theme::accent_hover, alpha(opacity + 0.14f)),
                    with_alpha(theme::accent_active, alpha(opacity + 0.26f)),
                    theme::is_dark() ? theme::text : theme::accent_ink};
        }
        return {theme::accent, theme::accent_hover, theme::accent_active, theme::accent_ink};
    case Tone::record:
        return {theme::field, theme::field_hover, theme::field_active, theme::text};
    case Tone::danger:
        if (theme::glass_enabled()) {
            return {theme::field,
                    with_alpha(theme::danger, 160),
                    with_alpha(theme::danger, 200),
                    theme::danger};
        }
        return {theme::field, theme::danger_soft, with_alpha(theme::danger, 56), theme::danger};
    case Tone::quiet:
        return {0, theme::field_hover, theme::field_active, theme::text_dim};
    case Tone::secondary:
    default:
        return {theme::field, theme::field_hover, theme::field_active, theme::text};
    }
}

ImU32 state_fill(const Colors& colors, const bool hovered, const bool held) {
    return held ? colors.fill_active : hovered ? colors.fill_hover : colors.fill;
}

// Set while any smoothed() call this frame is still short of its target, so
// the main loop knows to keep redrawing instead of going idle (see
// animations_active()). Cleared at the start of each frame.
bool g_animations_active = false;

// Eases `target` in over a few frames instead of snapping, keyed by `id` so
// many widgets can each animate independently with no state of their own.
// The first call for a given id starts at `target` (no pop-in from zero).
ImU32 smoothed_color(const ImGuiID id, const ImU32 target) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImU32 previous = static_cast<ImU32>(storage->GetInt(id, static_cast<int>(target)));
    if (previous == target)
        return target;
    // Exponential ease: framerate-independent, and it only ever approaches
    // target, so a snap-to-target close threshold below is what stops it.
    constexpr float speed = 22.0f;
    const float t = std::clamp(1.0f - std::exp(-speed * ImGui::GetIO().DeltaTime), 0.0f, 1.0f);
    const auto lerp_channel = [&](const int shift) -> ImU32 {
        const int from = static_cast<int>((previous >> shift) & 0xFF);
        const int to = static_cast<int>((target >> shift) & 0xFF);
        return static_cast<ImU32>(from + static_cast<int>(std::lround((to - from) * t))) << shift;
    };
    ImU32 next = lerp_channel(IM_COL32_R_SHIFT) | lerp_channel(IM_COL32_G_SHIFT) |
                lerp_channel(IM_COL32_B_SHIFT) | lerp_channel(IM_COL32_A_SHIFT);
    // Close enough: snap the last mile so this stops claiming to animate.
    const int diff = std::abs(static_cast<int>((next >> IM_COL32_R_SHIFT) & 0xFF) -
                              static_cast<int>((target >> IM_COL32_R_SHIFT) & 0xFF)) +
                     std::abs(static_cast<int>((next >> IM_COL32_G_SHIFT) & 0xFF) -
                              static_cast<int>((target >> IM_COL32_G_SHIFT) & 0xFF)) +
                     std::abs(static_cast<int>((next >> IM_COL32_B_SHIFT) & 0xFF) -
                              static_cast<int>((target >> IM_COL32_B_SHIFT) & 0xFF)) +
                     std::abs(static_cast<int>((next >> IM_COL32_A_SHIFT) & 0xFF) -
                              static_cast<int>((target >> IM_COL32_A_SHIFT) & 0xFF));
    if (diff <= 2)
        next = target;
    else
        g_animations_active = true;
    storage->SetInt(id, static_cast<int>(next));
    return next;
}

ImVec2 resolve_size(const ImVec2 size, const ImVec2 content) {
    ImVec2 result = size;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (result.x == 0.0f)
        result.x = content.x + px(28);
    else if (result.x < 0.0f)
        result.x = std::max(avail + result.x, px(10));
    if (result.y == 0.0f)
        result.y = ImGui::GetFrameHeight();
    return result;
}

} // namespace

float px(const float value) { return value * ImGui::GetStyle().FontScaleDpi; }

void begin_frame_animations() { g_animations_active = false; }
bool animations_active() { return g_animations_active; }

bool begin_card(const char* id, const char* title, const float height) {
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(12)));
    ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (height == 0.0f)
        flags |= ImGuiChildFlags_AutoResizeY;
    const bool open = ImGui::BeginChild(id, ImVec2(0, height), flags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (open)
        paint_card();
    if (open && title != nullptr)
        card_title(title);
    return open;
}

void end_card() { ImGui::EndChild(); }

void paint_glass(ImDrawList* list, const ImVec2 p0, const ImVec2 p1, const float rounding,
                 const ImU32 fill) {
    // Every colour goes through faded(), so a fading parent (a notice, a
    // new tab) fades its glass with it.
    if (!theme::glass_enabled()) {
        list->AddRectFilled(p0, p1, faded(fill != 0 ? fill : theme::surface), rounding);
        list->AddRect(p0, p1, faded(theme::border), rounding);
        return;
    }
    backdrop::draw_frosted(list, p0, p1, rounding);
    list->AddRectFilled(p0, p1, faded(fill != 0 ? fill : theme::surface), rounding);
    list->AddRect(p0, p1, faded(theme::border), rounding);
    // The gloss covers only the top 60 % (capped, so a tall panel keeps a
    // short highlight) and fades to nothing at its lower edge; the rim runs
    // from bright at the top to faint.
    const float gloss_end = p0.y + std::min((p1.y - p0.y) * 0.6f, px(160));
    int start = list->VtxBuffer.Size;
    list->AddRectFilled(p0, ImVec2(p1.x, gloss_end), faded(IM_COL32_WHITE), rounding,
                        ImDrawFlags_RoundCornersTop);
    fade_vertical(list, start, p0.y, gloss_end, theme::gloss_alpha, 0);
    start = list->VtxBuffer.Size;
    list->AddRect(p0, p1, faded(IM_COL32_WHITE), rounding);
    fade_vertical(list, start, p0.y, p1.y, theme::rim_top_alpha, theme::rim_bottom_alpha);
    draw_sheen(list, p0, p1, rounding);
}

void glass_popups() {
    for (ImGuiWindow* window : GImGui->Windows) {
        if (!window->Active || window->Hidden ||
            (window->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip)) == 0 ||
            (window->Flags & ImGuiWindowFlags_ChildWindow) != 0)
            continue;
        ImDrawList* list = window->DrawList;
        const ImVec2 p0 = window->Pos;
        const ImVec2 p1(p0.x + window->Size.x, p0.y + window->Size.y);
        // The glass goes into draw commands of its own, which are then moved
        // in front of the window's content: a draw list renders its commands
        // in order, and each carries its own vertex and index offsets.
        list->AddDrawCmd();
        const int first = list->CmdBuffer.Size - 1;
        list->PushClipRect(p0, p1);
        if (theme::glass_enabled()) {
            backdrop::draw_base(list, p0, p1, window->WindowRounding);
            paint_glass(list, p0, p1, window->WindowRounding);
        } else {
            list->AddRectFilled(p0, p1, faded(theme::popup_fill), window->WindowRounding);
            list->AddRect(p0, p1, faded(theme::border_strong), window->WindowRounding);
        }
        list->PopClipRect();
        std::rotate(list->CmdBuffer.Data, list->CmdBuffer.Data + first,
                    list->CmdBuffer.Data + list->CmdBuffer.Size);
    }
}

void paint_card() {
    const ImVec2 p0 = ImGui::GetWindowPos();
    const ImVec2 p1(p0.x + ImGui::GetWindowWidth(), p0.y + ImGui::GetWindowHeight());
    const float rounding = ImGui::GetStyle().ChildRounding;
    ImDrawList* list = ImGui::GetWindowDrawList();
    // The card's own edge sits outside its content clip, so clip to the card
    // instead, but never past what the parent shows (a scrolled-out card
    // must not paint over its neighbours).
    ImVec2 clip0 = p0;
    ImVec2 clip1 = p1;
    if (const ImGuiWindow* parent = ImGui::GetCurrentWindow()->ParentWindow) {
        clip0 = ImMax(clip0, parent->ClipRect.Min);
        clip1 = ImMin(clip1, parent->ClipRect.Max);
    }
    list->PushClipRect(clip0, clip1);
    paint_glass(list, p0, p1, rounding);
    list->PopClipRect();
}

void card_title(const char* text) {
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(nullptr, theme::font_title);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void caption(const char* text) {
    ImGui::PushFont(nullptr, theme::font_small);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_dim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void text_dim(const char* format, ...) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_dim);
    va_list args;
    va_start(args, format);
    ImGui::TextWrappedV(format, args);
    va_end(args);
    ImGui::PopStyleColor();
}

void text_colored(const ImU32 color, const char* format, ...) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    va_list args;
    va_start(args, format);
    ImGui::TextWrappedV(format, args);
    va_end(args);
    ImGui::PopStyleColor();
}

bool button(const char* label, const ImVec2 requested, const Tone tone) {
    const char* label_end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 text_size = ImGui::CalcTextSize(label, label_end);
    const ImVec2 size = resolve_size(requested, text_size);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    const bool pressed = ImGui::InvisibleButton(label, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    const Colors colors = colors_for(tone);
    ImDrawList* list = ImGui::GetWindowDrawList();
    const float rounding = ImGui::GetStyle().FrameRounding;
    const ImU32 fill = smoothed_color(ImGui::GetID(label), state_fill(colors, hovered, held));
    if ((fill & IM_COL32_A_MASK) != 0U) {
        list->AddRectFilled(p0, p1, faded(fill), rounding);
        if (theme::glass_enabled()) {
            const ImU32 border_col = (tone == Tone::primary)
                                         ? with_alpha(theme::accent_line, 160)
                                         : theme::border;
            list->AddRect(p0, p1, faded(border_col), rounding);
        }
        draw_sheen(list, p0, p1, rounding);
    }
    if (tone == Tone::danger && hovered && !theme::glass_enabled())
        list->AddRect(p0, p1, faded(with_alpha(theme::danger, 90)), rounding);
    // The record tone leads with a red dot; the pair is centred together.
    const float dot = tone == Tone::record ? px(10) : 0.0f;
    const float lead = dot > 0.0f ? dot + px(9) : 0.0f;
    const float x = p0.x + (size.x - text_size.x - lead) * 0.5f;
    if (dot > 0.0f)
        list->AddCircleFilled(ImVec2(x + dot * 0.5f, p0.y + size.y * 0.5f), dot * 0.5f,
                              faded(theme::danger), 20);
    list->AddText(ImVec2(x + lead, p0.y + (size.y - text_size.y) * 0.5f), faded(colors.text),
                  label, label_end);
    return pressed;
}

bool icon_key(const char* id, const Icon icon, const ImVec2 size, const Tone tone) {
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    const Colors colors = colors_for(tone);
    const ImU32 fill = smoothed_color(ImGui::GetID(id), state_fill(colors, hovered, held));
    ImDrawList* list = ImGui::GetWindowDrawList();
    const float rounding = ImGui::GetStyle().FrameRounding;
    if ((fill & IM_COL32_A_MASK) != 0U) {
        list->AddRectFilled(p0, p1, faded(fill), rounding);
        if (theme::glass_enabled()) {
            const ImU32 border_col = (tone == Tone::primary)
                                         ? with_alpha(theme::accent_line, 160)
                                         : theme::border;
            list->AddRect(p0, p1, faded(border_col), rounding);
        }
        draw_sheen(list, p0, p1, rounding);
    }
    draw_icon(list, icon, ImVec2((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f), size.y * 0.62f,
              faded(colors.text));
    return pressed;
}

bool icon_key_down(const char* id, const Icon icon, const ImVec2 size, const Tone tone) {
    icon_key(id, icon, size, tone);
    return ImGui::IsItemActivated();
}

void draw_icon(ImDrawList* list, const Icon icon, const ImVec2 c, const float s,
               const ImU32 color) {
    const float t = std::max(1.5f, s * 0.11f);
    switch (icon) {
    case Icon::play:
        list->AddTriangleFilled(ImVec2(c.x - s * 0.30f, c.y - s * 0.40f),
                                ImVec2(c.x - s * 0.30f, c.y + s * 0.40f),
                                ImVec2(c.x + s * 0.42f, c.y), color);
        break;
    case Icon::pause: {
        const float w = s * 0.20f;
        const float h = s * 0.38f;
        const float gap = s * 0.10f;
        list->AddRectFilled(ImVec2(c.x - gap - w, c.y - h), ImVec2(c.x - gap, c.y + h), color,
                            w * 0.25f);
        list->AddRectFilled(ImVec2(c.x + gap, c.y - h), ImVec2(c.x + gap + w, c.y + h), color,
                            w * 0.25f);
        break;
    }
    case Icon::stop: {
        const float h = s * 0.30f;
        list->AddRectFilled(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), color, h * 0.2f);
        break;
    }
    case Icon::restart: {
        const float h = s * 0.32f;
        list->AddRectFilled(ImVec2(c.x - h, c.y - h), ImVec2(c.x - h + t * 1.3f, c.y + h), color,
                            t * 0.3f);
        list->AddTriangleFilled(ImVec2(c.x + h, c.y - h), ImVec2(c.x + h, c.y + h),
                                ImVec2(c.x - h + t * 1.6f, c.y), color);
        break;
    }
    case Icon::record:
        list->AddCircleFilled(c, s * 0.32f, color, 32);
        break;
    case Icon::refresh: {
        const float r = s * 0.34f;
        const float a0 = -pi * 0.35f;
        const float a1 = pi * 1.35f;
        list->PathArcTo(c, r, a0, a1, 24);
        list->PathStroke(color, t);
        const ImVec2 tip(c.x + r * std::cos(a0), c.y + r * std::sin(a0));
        const float h = s * 0.18f;
        list->AddTriangleFilled(ImVec2(tip.x - h * 0.2f, tip.y - h * 1.1f),
                                ImVec2(tip.x + h * 1.1f, tip.y + h * 0.1f),
                                ImVec2(tip.x - h * 0.6f, tip.y + h * 0.6f), color);
        break;
    }
    case Icon::chevron_down:
    case Icon::chevron_right:
    case Icon::chevron_left: {
        const float h = s * 0.20f;
        ImVec2 points[3];
        if (icon == Icon::chevron_down) {
            points[0] = ImVec2(c.x - h * 1.4f, c.y - h * 0.6f);
            points[1] = ImVec2(c.x, c.y + h * 0.8f);
            points[2] = ImVec2(c.x + h * 1.4f, c.y - h * 0.6f);
        } else if (icon == Icon::chevron_left) {
            points[0] = ImVec2(c.x + h * 0.6f, c.y - h * 1.4f);
            points[1] = ImVec2(c.x - h * 0.8f, c.y);
            points[2] = ImVec2(c.x + h * 0.6f, c.y + h * 1.4f);
        } else {
            points[0] = ImVec2(c.x - h * 0.6f, c.y - h * 1.4f);
            points[1] = ImVec2(c.x + h * 0.8f, c.y);
            points[2] = ImVec2(c.x - h * 0.6f, c.y + h * 1.4f);
        }
        list->AddPolyline(points, 3, color, t);
        break;
    }
    case Icon::close: {
        const float h = s * 0.24f;
        list->AddLine(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), color, t);
        list->AddLine(ImVec2(c.x - h, c.y + h), ImVec2(c.x + h, c.y - h), color, t);
        break;
    }
    case Icon::up:
    case Icon::down: {
        const float h = s * 0.22f;
        const float d = icon == Icon::up ? -1.0f : 1.0f;
        list->AddTriangleFilled(ImVec2(c.x - h * 1.2f, c.y - d * h * 0.6f),
                                ImVec2(c.x + h * 1.2f, c.y - d * h * 0.6f),
                                ImVec2(c.x, c.y + d * h * 0.8f), color);
        break;
    }
    case Icon::search: {
        const float r = s * 0.24f;
        const ImVec2 lens(c.x - s * 0.07f, c.y - s * 0.07f);
        list->AddCircle(lens, r, color, 20, t);
        const float d = r * 0.7071f;
        list->AddLine(ImVec2(lens.x + d, lens.y + d), ImVec2(c.x + s * 0.33f, c.y + s * 0.33f),
                      color, t * 1.1f);
        break;
    }
    case Icon::expand: {
        // Two corner brackets, pointing out.
        const float a = s * 0.34f;
        const float b = s * 0.12f;
        for (int corner = 0; corner < 2; ++corner) {
            const float sx = corner == 0 ? -1.0f : 1.0f;
            const ImVec2 tip(c.x + sx * a, c.y + sx * a);
            list->AddLine(tip, ImVec2(c.x + sx * b, c.y + sx * a), color, t);
            list->AddLine(tip, ImVec2(c.x + sx * a, c.y + sx * b), color, t);
        }
        break;
    }
    case Icon::trash: {
        const float w = s * 0.24f;
        const float h = s * 0.30f;
        // Lid with its handle, then the body.
        list->AddLine(ImVec2(c.x - w * 1.3f, c.y - h * 0.80f),
                      ImVec2(c.x + w * 1.3f, c.y - h * 0.80f), color, t);
        list->AddLine(ImVec2(c.x - w * 0.45f, c.y - h * 1.15f),
                      ImVec2(c.x + w * 0.45f, c.y - h * 1.15f), color, t);
        list->AddRect(ImVec2(c.x - w, c.y - h * 0.55f), ImVec2(c.x + w, c.y + h), color, t * 0.8f,
                      t);
        break;
    }
    case Icon::check: {
        const ImVec2 points[3] = {ImVec2(c.x - s * 0.26f, c.y + s * 0.02f),
                                  ImVec2(c.x - s * 0.07f, c.y + s * 0.20f),
                                  ImVec2(c.x + s * 0.27f, c.y - s * 0.18f)};
        list->AddPolyline(points, 3, color, std::max(1.5f, s * 0.12f));
        break;
    }
    case Icon::monitor: {
        const float w = s * 0.36f;
        const float h = s * 0.26f;
        list->AddRect(ImVec2(c.x - w, c.y - h), ImVec2(c.x + w, c.y + h), color, t * 0.5f, t);
        list->AddLine(ImVec2(c.x - w * 0.4f, c.y + h + t * 1.6f),
                      ImVec2(c.x + w * 0.4f, c.y + h + t * 1.6f), color, t);
        break;
    }
    case Icon::list: {
        const float w = s * 0.32f;
        const float step = s * 0.22f;
        for (int row = -1; row <= 1; ++row) {
            const float y = c.y + static_cast<float>(row) * step;
            const float end = row == 1 ? w * 0.55f : w;
            list->AddLine(ImVec2(c.x - w, y), ImVec2(c.x + end, y), color, t);
        }
        break;
    }
    case Icon::terminal: {
        const float w = s * 0.34f;
        const float h = s * 0.18f;
        const float x = c.x - w;
        list->AddLine(ImVec2(x, c.y - h), ImVec2(x + w * 0.6f, c.y), color, t);
        list->AddLine(ImVec2(x + w * 0.6f, c.y), ImVec2(x, c.y + h), color, t);
        list->AddLine(ImVec2(c.x + w * 0.05f, c.y + h), ImVec2(c.x + w, c.y + h), color, t);
        break;
    }
    case Icon::play_pause: {
        list->AddTriangleFilled(ImVec2(c.x - s * 0.40f, c.y - s * 0.30f),
                                ImVec2(c.x - s * 0.40f, c.y + s * 0.30f),
                                ImVec2(c.x - s * 0.06f, c.y), color);
        const float w = s * 0.10f;
        const float h = s * 0.30f;
        list->AddRectFilled(ImVec2(c.x + s * 0.06f, c.y - h), ImVec2(c.x + s * 0.06f + w, c.y + h),
                            color, w * 0.25f);
        list->AddRectFilled(ImVec2(c.x + s * 0.24f, c.y - h), ImVec2(c.x + s * 0.24f + w, c.y + h),
                            color, w * 0.25f);
        break;
    }
    case Icon::prev:
    case Icon::next: {
        // A bar and a triangle, mirrored for next.
        const float d = icon == Icon::next ? 1.0f : -1.0f;
        const float bar = c.x + d * s * 0.30f;
        list->AddRectFilled(ImVec2(bar - t * 0.65f, c.y - s * 0.34f),
                            ImVec2(bar + t * 0.65f, c.y + s * 0.34f), color, t * 0.3f);
        list->AddTriangleFilled(ImVec2(c.x - d * s * 0.34f, c.y - s * 0.34f),
                                ImVec2(c.x - d * s * 0.34f, c.y + s * 0.34f),
                                ImVec2(c.x + d * s * 0.22f, c.y), color);
        break;
    }
    case Icon::plus:
    case Icon::minus: {
        const float h = s * 0.26f;
        list->AddLine(ImVec2(c.x - h, c.y), ImVec2(c.x + h, c.y), color, t * 1.15f);
        if (icon == Icon::plus)
            list->AddLine(ImVec2(c.x, c.y - h), ImVec2(c.x, c.y + h), color, t * 1.15f);
        break;
    }
    case Icon::gear: {
        // Eight square teeth around a ring with a hole.
        const float r = s * 0.24f;
        const float tooth = s * 0.10f;
        for (int index = 0; index < 8; ++index) {
            const float a = static_cast<float>(index) * (pi / 4.0f);
            const ImVec2 d(std::cos(a), std::sin(a));
            list->AddLine(ImVec2(c.x + d.x * r, c.y + d.y * r),
                          ImVec2(c.x + d.x * (r + tooth * 1.3f), c.y + d.y * (r + tooth * 1.3f)),
                          color, tooth * 1.6f);
        }
        list->AddCircle(c, r, color, 24, t * 1.6f);
        list->AddCircle(c, s * 0.09f, color, 16, t);
        break;
    }
    }
}

void draw_sheen(ImDrawList* list, const ImVec2 p0, const ImVec2 p1, const float rounding) {
    if (!theme::glass_enabled())
        return;
    const float inset = std::max(rounding, 1.0f);
    if (p1.x - p0.x <= inset * 2.0f)
        return;
    list->AddLine(ImVec2(p0.x + inset, p0.y + 1.0f), ImVec2(p1.x - inset, p0.y + 1.0f),
                  faded(theme::sheen));
}

void draw_check(ImDrawList* list, const ImVec2 p0, const float size, const bool checked,
                const ImU32 fill) {
    const ImVec2 p1(p0.x + size, p0.y + size);
    const float rounding = size * 0.25f;
    if (checked) {
        const ImU32 box = fill != 0 ? fill
                                    : (theme::glass_enabled() ? with_alpha(theme::accent, 185)
                                                              : theme::accent);
        list->AddRectFilled(p0, p1, faded(box), rounding);
        if (theme::glass_enabled()) {
            list->AddRect(p0, p1, faded(with_alpha(theme::accent_line, 160)), rounding);
            draw_sheen(list, p0, p1, rounding);
        }
        draw_icon(list, Icon::check, ImVec2(p0.x + size * 0.5f, p0.y + size * 0.5f), size,
                  faded(theme::accent_ink));
    } else {
        list->AddRect(p0, p1, faded(theme::border_strong), rounding, px(1.5f));
    }
}

namespace {
bool is_utf8_lead_or_ascii(const char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }
} // namespace

void draw_text_ellipsized(ImDrawList* const list, const ImVec2 pos, const ImU32 color,
                          const char* const text, const float max_width) {
    const size_t length = std::strlen(text);
    if (max_width <= 0.0f || ImGui::CalcTextSize(text, text + length).x <= max_width) {
        list->AddText(pos, color, text);
        return;
    }
    constexpr char ellipsis[] = "...";
    const float ellipsis_width = ImGui::CalcTextSize(ellipsis).x;
    if (ellipsis_width >= max_width) {
        list->AddText(pos, color, ".");
        return;
    }
    const float budget = max_width - ellipsis_width;
    // Longest byte-prefix whose rendered width still fits `budget`.
    size_t lo = 0, hi = length;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo + 1) / 2;
        if (ImGui::CalcTextSize(text, text + mid).x <= budget)
            lo = mid;
        else
            hi = mid - 1;
    }
    while (lo > 0 && !is_utf8_lead_or_ascii(text[lo]))
        --lo;
    const std::string truncated = std::string(text, lo) + ellipsis;
    list->AddText(pos, color, truncated.c_str());
}

bool icon_button(const char* id, const Icon icon, const float diameter, const Tone tone,
                 const char* tooltip) {
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(diameter, diameter));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const ImVec2 c(p0.x + diameter * 0.5f, p0.y + diameter * 0.5f);
    ImDrawList* list = ImGui::GetWindowDrawList();

    const Colors colors = colors_for(tone);
    const ImU32 fill = smoothed_color(ImGui::GetID(id), state_fill(colors, hovered, held));
    if ((fill & IM_COL32_A_MASK) != 0U) {
        list->AddCircleFilled(c, diameter * 0.5f, faded(fill), 48);
        if (theme::glass_enabled()) {
            const ImU32 border_col = (tone == Tone::primary)
                                         ? with_alpha(theme::accent_line, 160)
                                         : theme::border;
            list->AddCircle(c, diameter * 0.5f, faded(border_col), 48);
        }
    }
    const ImU32 glyph = tone == Tone::quiet && hovered ? theme::text : colors.text;
    const float scale = tone == Tone::quiet ? 0.62f : 0.48f;
    draw_icon(list, icon, c, diameter * scale, faded(glyph));
    if (tooltip != nullptr)
        ImGui::SetItemTooltip("%s", tooltip);
    return pressed;
}

bool search_box(const char* id, std::string* text, const float width, const char* hint,
                const bool focus, bool* active_out) {
    ImGui::PushID(id);
    const float row = ImGui::GetFrameHeight();
    const float icon_room = px(28);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(icon_room, ImGui::GetStyle().FramePadding.y));
    if (focus)
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(width);
    ImGui::SetNextItemAllowOverlap();
    const bool entered = ImGui::InputTextWithHint(
        "##field", hint, text,
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll);
    const bool active = ImGui::IsItemActive();
    if (active_out != nullptr)
        *active_out = active;
    ImGui::PopStyleVar();

    ImDrawList* list = ImGui::GetWindowDrawList();
    draw_icon(list, Icon::search, ImVec2(p0.x + icon_room * 0.55f, p0.y + row * 0.5f), row * 0.72f,
              faded(active || !text->empty() ? theme::text_dim : theme::text_faint));
    if (!text->empty()) {
        const float size = row - px(8);
        ImGui::SetCursorScreenPos(ImVec2(p0.x + width - size - px(4), p0.y + px(4)));
        if (ImGui::InvisibleButton("##clear", ImVec2(size, size)))
            text->clear();
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 c(p0.x + width - size * 0.5f - px(4), p0.y + row * 0.5f);
        if (hovered)
            list->AddCircleFilled(c, size * 0.5f, faded(theme::field_active), 20);
        draw_icon(list, Icon::close, c, size, faded(hovered ? theme::text : theme::text_dim));
        ImGui::SetItemTooltip("Clear (Esc)");
        // End on a zero-width item at the field's right edge, so the layout
        // (SameLine included) matches the field without the button.
        ImGui::SetCursorScreenPos(ImVec2(p0.x + width, p0.y));
        ImGui::Dummy(ImVec2(0, row));
    }
    ImGui::PopID();
    return entered;
}

bool escape_pressed() {
    // Read from the key's own state: an active text field owns Escape, and
    // the owner-aware queries would hide the press from the picker.
    const ImGuiKeyData* key = ImGui::GetKeyData(ImGuiKey_Escape);
    return key != nullptr && key->Down && key->DownDuration == 0.0f;
}

bool toggle(const char* label, bool* value) {
    const char* label_end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 text_size = ImGui::CalcTextSize(label, label_end);
    const float row = ImGui::GetFrameHeight();
    const float height = std::round(row * 0.66f);
    const float width = std::round(height * 1.75f);
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x + px(4);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(width + spacing + text_size.x, row));
    if (pressed)
        *value = !*value;
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 t0(p0.x, p0.y + (row - height) * 0.5f);
    const ImVec2 t1(t0.x + width, t0.y + height);
    const float radius = height * 0.5f;

    if (theme::glass_enabled()) {
        const ImU32 track = *value
            ? (hovered ? with_alpha(theme::accent_hover, 210) : with_alpha(theme::accent, 175))
            : (hovered ? with_alpha(theme::field_hover, 70) : with_alpha(theme::field, 40));
        list->AddRectFilled(t0, t1, faded(track), radius);
        const ImU32 track_border = *value ? with_alpha(theme::accent_line, 170) : theme::border;
        list->AddRect(t0, t1, faded(track_border), radius);
        draw_sheen(list, t0, t1, radius);

        const float knob_x = *value ? t1.x - radius : t0.x + radius;
        const ImVec2 knob_center(knob_x, t0.y + radius);
        const float knob_r = radius - px(2.5f);
        if (*value) {
            list->AddCircleFilled(knob_center, knob_r, faded(IM_COL32_WHITE), 24);
            list->AddCircle(knob_center, knob_r, faded(with_alpha(IM_COL32_WHITE, 180)), 24);
            list->AddCircleFilled(ImVec2(knob_center.x - knob_r * 0.3f, knob_center.y - knob_r * 0.3f),
                                  knob_r * 0.28f, faded(with_alpha(IM_COL32_WHITE, 220)), 12);
        } else {
            list->AddCircleFilled(knob_center, knob_r,
                                  faded(hovered ? with_alpha(IM_COL32_WHITE, 120)
                                                : with_alpha(IM_COL32_WHITE, 80)), 24);
            list->AddCircle(knob_center, knob_r, faded(with_alpha(IM_COL32_WHITE, 140)), 24);
            list->AddCircleFilled(ImVec2(knob_center.x - knob_r * 0.3f, knob_center.y - knob_r * 0.3f),
                                  knob_r * 0.25f, faded(with_alpha(IM_COL32_WHITE, 160)), 12);
        }
    } else {
        const ImU32 track = *value ? (hovered ? theme::accent_hover : theme::accent)
                                   : (hovered ? theme::field_hover : theme::field);
        list->AddRectFilled(t0, t1, faded(track), radius);
        if (!*value)
            list->AddRect(t0, t1, faded(theme::border), radius);
        const float knob_x = *value ? t1.x - radius : t0.x + radius;
        list->AddCircleFilled(ImVec2(knob_x, t0.y + radius), radius - px(2.5f),
                              faded(*value ? IM_COL32_WHITE : theme::text_dim), 24);
    }

    list->AddText(ImVec2(t1.x + spacing, p0.y + (row - text_size.y) * 0.5f), faded(theme::text),
                  label, label_end);
    return pressed;
}

float status_item_width(const char* text) {
    ImGui::PushFont(nullptr, theme::font_small);
    const float width = ImGui::CalcTextSize(text).x + px(3) * 2 + px(10);
    ImGui::PopFont();
    return width;
}

// A small square dot and its label, set directly on the header's own
// background — no pill, no border. A dense status readout (like a
// hardware panel's indicator lights) rather than a row of chips.
void status_item(const char* text, const ImU32 dot, const bool blink) {
    ImGui::PushFont(nullptr, theme::font_small);
    const ImVec2 text_size = ImGui::CalcTextSize(text);
    const float dot_size = px(6);
    const ImVec2 size(dot_size + px(7) + text_size.x, text_size.y);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 d0(p0.x, p0.y + (text_size.y - dot_size) * 0.5f);
    ImU32 dot_color = dot;
    if (blink) {
        const float phase = static_cast<float>(std::fmod(ImGui::GetTime(), 1.2) / 1.2);
        const float level = 0.45f + 0.55f * (0.5f + 0.5f * std::cos(phase * 2.0f * pi));
        dot_color = with_alpha(dot, static_cast<unsigned>(level * 255.0f));
    }
    list->AddRectFilled(d0, ImVec2(d0.x + dot_size, d0.y + dot_size), faded(dot_color), px(1));
    list->AddText(ImVec2(p0.x + dot_size + px(7), p0.y), faded(theme::text_dim), text);
    ImGui::PopFont();
}

// A thin vertical rule between status_item()s, the width of one space.
void status_divider() {
    const float height = px(11);
    ImGui::PushFont(nullptr, theme::font_small);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(px(1), ImGui::GetTextLineHeight()));
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(p0.x, p0.y + (ImGui::GetTextLineHeight() - height) * 0.5f),
        ImVec2(p0.x + px(1), p0.y + (ImGui::GetTextLineHeight() + height) * 0.5f),
        faded(theme::border_strong));
    ImGui::PopFont();
}

void spinner(const float radius, const ImU32 color) {
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float row = ImGui::GetFrameHeight();
    ImGui::Dummy(ImVec2(radius * 2, row));
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 c(p0.x + radius, p0.y + row * 0.5f);
    list->AddCircle(c, radius - px(1), faded(theme::field_active), 24, px(2));
    const float start = static_cast<float>(ImGui::GetTime() * 5.0);
    list->PathArcTo(c, radius - px(1), start, start + pi * 0.6f, 12);
    list->PathStroke(faded(color), px(2));
}

bool scrub_bar(const char* id, const float fraction, const float width, const float height,
               float* preview, ScrubState* state, const ImU32 fill) {
    const ImU32 fill_color = fill != 0 ? fill : theme::accent;
    const float row = std::max(height + px(14), px(20));
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(width, row));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool released = ImGui::IsItemDeactivated();
    const float mouse = std::clamp((ImGui::GetIO().MousePos.x - p0.x) / width, 0.0f, 1.0f);
    if (active)
        *preview = mouse;
    if (state != nullptr) {
        state->hovered = hovered;
        state->dragging = active;
        state->hover_fraction = mouse;
    }

    const float shown = std::clamp(active ? *preview : fraction, 0.0f, 1.0f);
    ImDrawList* list = ImGui::GetWindowDrawList();
    const float cy = std::round(p0.y + row * 0.5f);
    const float radius = height * 0.5f;
    const ImVec2 t0(p0.x, cy - radius);
    const ImVec2 t1(p0.x + width, cy + radius);
    list->AddRectFilled(t0, t1, faded(theme::field_active), radius);
    const float fill_x = p0.x + width * shown;
    if (hovered && !active) {
        // Where a click would land.
        const float x = p0.x + width * mouse;
        list->AddLine(ImVec2(x, cy - height * 1.6f), ImVec2(x, cy + height * 1.6f),
                      faded(theme::text_dim), px(1));
    }
    if (fill_x > t0.x + 0.5f)
        list->AddRectFilled(t0, ImVec2(std::max(fill_x, t0.x + height), t1.y), faded(fill_color),
                            radius);
    const float knob = (hovered || active) ? height * 1.5f : height * 1.2f;
    list->AddCircleFilled(ImVec2(fill_x, cy), knob, faded(IM_COL32(245, 245, 248, 255)), 24);
    return released;
}

bool slider(const char* id, float* value, const float minimum, const float maximum,
            const int decimals, const char* unit, const float step, const bool logarithmic) {
    const auto to_fraction = [&](const float v) {
        if (logarithmic)
            return std::log(v / minimum) / std::log(maximum / minimum);
        return (v - minimum) / (maximum - minimum);
    };
    const auto from_fraction = [&](const float f) {
        float v = logarithmic ? minimum * std::pow(maximum / minimum, f)
                              : minimum + (maximum - minimum) * f;
        if (step > 0.0f)
            v = std::round(v / step) * step;
        return std::clamp(v, minimum, maximum);
    };

    const auto print = [&](char* out, const size_t size, const float v) {
        std::snprintf(out, size, "%.*f%s", decimals, static_cast<double>(v), unit);
    };
    char text[48];
    print(text, sizeof text, *value);
    char widest[48];
    print(widest, sizeof widest, maximum);
    const float value_width = std::max(ImGui::CalcTextSize(widest).x, px(34));

    const float row = ImGui::GetFrameHeight();
    const float total = ImGui::GetContentRegionAvail().x;
    const float track_width = std::max(total - value_width - px(14), px(40));
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float knob = px(8);
    ImGui::InvisibleButton(id, ImVec2(track_width, row));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    bool changed = false;
    if (active) {
        const float inner = track_width - knob * 2;
        const float f =
            std::clamp((ImGui::GetIO().MousePos.x - p0.x - knob) / std::max(inner, 1.0f), 0.0f,
                       1.0f);
        const float next = from_fraction(f);
        if (next != *value) {
            *value = next;
            changed = true;
            print(text, sizeof text, *value);
        }
    }

    ImDrawList* list = ImGui::GetWindowDrawList();
    const float cy = std::round(p0.y + row * 0.5f);
    const float track = px(6);
    const float x0 = p0.x + knob;
    const float x1 = p0.x + track_width - knob;
    const float x = x0 + (x1 - x0) * std::clamp(to_fraction(*value), 0.0f, 1.0f);
    list->AddRectFilled(ImVec2(x0, cy - track * 0.5f), ImVec2(x1, cy + track * 0.5f),
                        faded(theme::field_active), track * 0.5f);
    const ImU32 fill_accent = theme::glass_enabled() ? with_alpha(theme::accent, 185) : theme::accent;
    list->AddRectFilled(ImVec2(x0, cy - track * 0.5f), ImVec2(x, cy + track * 0.5f),
                        faded(fill_accent), track * 0.5f);
    if (theme::glass_enabled()) {
        list->AddRect(ImVec2(x0, cy - track * 0.5f), ImVec2(x1, cy + track * 0.5f),
                      faded(theme::border), track * 0.5f);
    }
    const float r = (hovered || active) ? knob : knob - px(1);
    list->AddCircleFilled(ImVec2(x, cy), r, faded(IM_COL32(245, 245, 248, 255)), 24);
    if (theme::glass_enabled()) {
        list->AddCircle(ImVec2(x, cy), r, faded(with_alpha(IM_COL32_WHITE, 160)), 24);
    }

    ImGui::SameLine(0, px(14));
    ImGui::AlignTextToFramePadding();
    const float text_x = ImGui::GetCursorPosX() + value_width - ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX(text_x);
    ImGui::PushStyleColor(ImGuiCol_Text, active ? theme::text : theme::text_dim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    return changed;
}

bool slider_int(const char* id, int* value, const int minimum, const int maximum,
                const char* unit) {
    float v = static_cast<float>(*value);
    const bool changed = slider(id, &v, static_cast<float>(minimum), static_cast<float>(maximum),
                                0, unit, 1.0f);
    if (changed)
        *value = static_cast<int>(std::lround(v));
    return changed;
}

void align_right(const float width) {
    ImGui::SameLine();
    const float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
    ImGui::SetCursorPosX(std::max(x, ImGui::GetCursorPosX()));
}

} // namespace gui::ui
