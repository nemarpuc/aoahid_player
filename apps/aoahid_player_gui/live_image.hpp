// SPDX-License-Identifier: MIT
#pragma once

#include <imgui.h>

#include <string>

namespace gui {

// One reference picture shown over the Live preview (e.g. a screenshot of
// the phone's UI), so live control can be aimed without guessing. Purely a
// visual aid: it never reaches the device. While unlocked it captures the
// pointer for its own move/resize/rotate handles instead of Live control;
// once locked it draws underneath the touch dot and lets every click and
// drag pass straight through to touch/mouse forwarding. State (see
// snapshot()/restore()) is saved between runs; the image itself is reloaded
// from `path` at startup if the file is still there.
class LiveImageOverlay {
  public:
    LiveImageOverlay() = default;
    ~LiveImageOverlay();

    LiveImageOverlay(const LiveImageOverlay&) = delete;
    LiveImageOverlay& operator=(const LiveImageOverlay&) = delete;

    // Decodes the file and uploads it as a texture, replacing whatever was
    // loaded before, centred in the preview at a modest default size.
    // Returns an empty string on success, or a message to show the user.
    [[nodiscard]] std::string load(const std::string& path);
    void clear();
    [[nodiscard]] bool loaded() const noexcept { return texture_ != 0; }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    // Opacity the image draws at, 0 (invisible) to 1 (opaque); does not
    // affect the outline or drag handles.
    [[nodiscard]] float opacity() const noexcept { return alpha_; }
    void set_opacity(float value) noexcept;
    // Back to the default position, size, and rotation; opacity is untouched.
    void reset_transform() noexcept;

    // Everything worth remembering between runs.
    struct State {
        std::string path;
        ImVec2 center{0.5f, 0.5f};
        float half_width_frac{0.35f};
        float rotation{0.0f};
        float alpha{1.0f};
    };
    [[nodiscard]] State snapshot() const;
    // Reloads `state.path` (a no-op if empty) and applies the rest. Returns
    // the same error load() would on a failed reload, or an empty string.
    std::string restore(const State& state);

    bool locked{false};

    // Call once per frame the preview surface is drawn, before deciding
    // whether Live control should also see the pointer. `surface_min` and
    // `surface_size` are the preview's screen rectangle. Returns true when
    // the pointer is being used to move, resize, or rotate the image, so
    // the caller should not also forward it as a touch or mouse event.
    bool interact(ImVec2 surface_min, ImVec2 surface_size);

    // Draws the image, and, while unlocked, its outline and handles.
    void draw(ImDrawList* list, ImVec2 surface_min, ImVec2 surface_size) const;

  private:
    enum class Drag { none, move, resize, rotate };

    unsigned int texture_{}; // 0 = nothing loaded
    int pixel_width_{};
    int pixel_height_{};
    std::string path_;

    ImVec2 center_{0.5f, 0.5f};    // fraction of the preview rectangle
    float half_width_frac_{0.35f}; // half-width as a fraction of the preview's width
    float rotation_{0.0f};         // radians, clockwise on screen
    float alpha_{1.0f};

    Drag dragging_{Drag::none};
    ImVec2 drag_offset_{};
    float drag_start_half_width_{};
    float drag_start_distance_{1.0f};
    float drag_start_rotation_{};
    float drag_start_angle_{};
};

} // namespace gui
