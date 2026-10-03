// SPDX-License-Identifier: MIT
#include "live_image.hpp"

#include "widgets.hpp"

#include "aoahid_player/paths.hpp"

#include <imgui_impl_opengl3_loader.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#include <stb_image.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace gui {
namespace {

using ui::px;

// Handles sit a little outside the image itself; both are generous so they
// stay easy to grab even on a small preview.
constexpr float rotate_handle_gap = 26.0f;
constexpr float handle_hit_radius = 15.0f;
constexpr float click_margin = 48.0f; // lets the rotate handle poke past the edge
constexpr float min_half_width_frac = 0.02f;
constexpr float max_half_width_frac = 4.0f;

float distance(const ImVec2 a, const ImVec2 b) noexcept {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

// Reads the whole file into memory; stb_image is built with STBI_NO_STDIO so
// decoding never has to guess how to open a UTF-8 path on Windows, the same
// filesystem boundary the rest of the project crosses through utf8_path().
bool read_file(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream file(aoap::utf8_path(path), std::ios::binary | std::ios::ate);
    if (!file)
        return false;
    const std::streamoff size = file.tellg();
    if (size < 0)
        return false;
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (!out.empty() && !file.read(reinterpret_cast<char*>(out.data()),
                                   static_cast<std::streamsize>(out.size())))
        return false;
    return true;
}

struct UnsupportedFormat {
    const char* description{};
    std::string_view extension{};
};

// The name and common extension of a picture format this program cannot
// read, recognised by the file's first bytes.
UnsupportedFormat recognize_unsupported(const std::vector<unsigned char>& bytes) {
    const auto at = [&](const size_t offset, const std::string_view text) {
        return bytes.size() >= offset + text.size() &&
               std::equal(text.begin(), text.end(), bytes.begin() + static_cast<long>(offset),
                          [](const char a, const unsigned char b) {
                              return static_cast<unsigned char>(a) == b;
                          });
    };
    if (at(0, "RIFF") && at(8, "WEBP"))
        return {"a WebP image", ".webp"};
    if (at(4, "ftyp")) {
        if (at(8, "avif") || at(8, "avis"))
            return {"an AVIF image", ".avif"};
        if (at(8, "heic") || at(8, "heix") || at(8, "mif1") || at(8, "msf1") || at(8, "hevc"))
            return {"a HEIC image", ".heic"};
    }
    // Spelled with their lengths: both signatures contain a zero byte.
    if (at(0, std::string_view("II*\0", 4)) || at(0, std::string_view("MM\0*", 4)))
        return {"a TIFF image", ".tiff"};
    if (at(0, "\xFF\x0A") || at(4, "JXL "))
        return {"a JPEG XL image", ".jxl"};
    if (at(0, "<svg"))
        return {"an SVG image", ".svg"};
    if (at(0, "<?xml")) {
        const size_t limit = std::min(bytes.size(), static_cast<size_t>(1024));
        const std::string_view head(reinterpret_cast<const char*>(bytes.data()), limit);
        if (head.find("<svg") != std::string_view::npos)
            return {"an SVG image", ".svg"};
    }
    if (at(0, "%PDF"))
        return {"a PDF", ".pdf"};
    return {};
}

} // namespace

std::string decode_image(const std::string& path, DecodedImage& image) {
    std::vector<unsigned char> bytes;
    if (!read_file(path, bytes) || bytes.empty())
        return "Could not read \"" + path + "\".";
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                                  &image.width, &image.height, &channels, 4);
    if (pixels == nullptr) {
        // A file named .png or .jpg is often something else inside (a WebP
        // saved from a browser); say what it is and what to do about it.
        if (const auto [format, expected_ext] = recognize_unsupported(bytes); format != nullptr) {
            std::filesystem::path p = aoap::utf8_path(path);
            std::string ext = aoap::path_utf8(p.extension());
            for (char& c : ext)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            const bool matches = (!expected_ext.empty() && ext == expected_ext) ||
                                 (expected_ext == ".tiff" && ext == ".tif") ||
                                 (expected_ext == ".avif" && ext == ".avis");
            const std::string qualifier = matches ? "" : ", whatever its name says";
            return "\"" + path + "\" is " + format + qualifier +
                   ". Save it as PNG, JPG, or BMP and choose that file.";
        }
        const char* reason = stbi_failure_reason();
        return "Could not decode \"" + path + "\"" +
               (reason != nullptr ? std::string(": ") + reason : std::string()) + ".";
    }
    image.pixels = {pixels, [](void* memory) { stbi_image_free(memory); }};
    return {};
}

LiveImageOverlay::~LiveImageOverlay() { clear(); }

std::string LiveImageOverlay::load(const std::string& path) {
    DecodedImage image;
    std::string error = decode_image(path, image);
    if (!error.empty())
        return error;
    const int width = image.width;
    const int height = image.height;

    clear();
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 image.pixels.get());
    glBindTexture(GL_TEXTURE_2D, 0);

    texture_ = texture;
    pixel_width_ = width;
    pixel_height_ = height;
    path_ = path;
    center_ = ImVec2(0.5f, 0.5f);
    half_width_frac_ = 0.35f;
    rotation_ = 0.0f;
    alpha_ = 1.0f;
    dragging_ = Drag::none;
    return {};
}

void LiveImageOverlay::set_opacity(const float value) noexcept {
    alpha_ = std::clamp(value, 0.0f, 1.0f);
}

void LiveImageOverlay::reset_transform() noexcept {
    center_ = ImVec2(0.5f, 0.5f);
    half_width_frac_ = 0.35f;
    rotation_ = 0.0f;
}

LiveImageOverlay::State LiveImageOverlay::snapshot() const {
    return State{path_, center_, half_width_frac_, rotation_, alpha_};
}

std::string LiveImageOverlay::restore(const State& state) {
    std::string error;
    if (!state.path.empty())
        error = load(state.path); // resets center_/half_width_frac_/rotation_/alpha_ first
    center_ = state.center;
    half_width_frac_ = state.half_width_frac;
    rotation_ = state.rotation;
    alpha_ = std::clamp(state.alpha, 0.0f, 1.0f);
    return error;
}

void LiveImageOverlay::clear() {
    if (texture_ != 0) {
        const GLuint texture = static_cast<GLuint>(texture_);
        glDeleteTextures(1, &texture);
        texture_ = 0;
    }
    pixel_width_ = 0;
    pixel_height_ = 0;
    path_.clear();
    dragging_ = Drag::none;
}

bool LiveImageOverlay::interact(const ImVec2 surface_min, const ImVec2 surface_size) {
    if (!loaded() || locked) {
        dragging_ = Drag::none;
        return false;
    }

    const ImVec2 center_px(surface_min.x + center_.x * surface_size.x,
                           surface_min.y + center_.y * surface_size.y);
    const float half_w = half_width_frac_ * surface_size.x;
    const float half_h = half_w * (static_cast<float>(pixel_height_) /
                                   static_cast<float>(std::max(pixel_width_, 1)));
    const float cosr = std::cos(rotation_);
    const float sinr = std::sin(rotation_);
    const auto to_screen = [&](const ImVec2 local) {
        return ImVec2(center_px.x + local.x * cosr - local.y * sinr,
                      center_px.y + local.x * sinr + local.y * cosr);
    };
    const ImVec2 resize_handle = to_screen(ImVec2(half_w, half_h));
    const ImVec2 rotate_handle = to_screen(ImVec2(0.0f, -half_h - px(rotate_handle_gap)));

    const ImGuiIO& io = ImGui::GetIO();
    const bool mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);

    if (dragging_ == Drag::none) {
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            return false;
        const ImVec2 mouse = io.MousePos;
        // A coarse gate first, so a click nowhere near the preview never
        // starts a drag just because the math below would technically hit.
        if (mouse.x < surface_min.x - px(click_margin) ||
            mouse.x > surface_min.x + surface_size.x + px(click_margin) ||
            mouse.y < surface_min.y - px(click_margin) ||
            mouse.y > surface_min.y + surface_size.y + px(click_margin))
            return false;

        const float hit_radius = px(handle_hit_radius);
        if (distance(mouse, rotate_handle) <= hit_radius) {
            dragging_ = Drag::rotate;
            drag_start_rotation_ = rotation_;
            drag_start_angle_ = std::atan2(mouse.y - center_px.y, mouse.x - center_px.x);
            return true;
        }
        if (distance(mouse, resize_handle) <= hit_radius) {
            dragging_ = Drag::resize;
            drag_start_half_width_ = half_width_frac_;
            drag_start_distance_ = std::max(1.0f, distance(mouse, center_px));
            return true;
        }
        // Inside the (possibly rotated) rectangle: undo the rotation, then
        // it is an ordinary axis-aligned bounds check.
        const float dx = mouse.x - center_px.x;
        const float dy = mouse.y - center_px.y;
        const float local_x = dx * cosr + dy * sinr;
        const float local_y = -dx * sinr + dy * cosr;
        if (std::fabs(local_x) > half_w || std::fabs(local_y) > half_h)
            return false;
        dragging_ = Drag::move;
        drag_offset_ = ImVec2(dx, dy);
        return true;
    }

    if (!mouse_down || !ImGui::IsMousePosValid(&io.MousePos)) {
        // The second condition also covers the pointer being captured for
        // mouse mode mid-drag (its io.MousePos is pinned to ImGui's "no
        // mouse" sentinel — see App::frame()): the drag simply stops rather
        // than snapping to whatever that sentinel resolves to.
        dragging_ = Drag::none;
        return true; // still consume the release frame
    }
    const ImVec2 mouse = io.MousePos;
    switch (dragging_) {
    case Drag::move: {
        const ImVec2 new_center(mouse.x - drag_offset_.x, mouse.y - drag_offset_.y);
        center_.x = std::clamp((new_center.x - surface_min.x) / surface_size.x, 0.0f, 1.0f);
        center_.y = std::clamp((new_center.y - surface_min.y) / surface_size.y, 0.0f, 1.0f);
        break;
    }
    case Drag::resize: {
        const float dist = std::max(1.0f, distance(mouse, center_px));
        half_width_frac_ = std::clamp(drag_start_half_width_ * (dist / drag_start_distance_),
                                      min_half_width_frac, max_half_width_frac);
        break;
    }
    case Drag::rotate: {
        const float angle = std::atan2(mouse.y - center_px.y, mouse.x - center_px.x);
        rotation_ = drag_start_rotation_ + (angle - drag_start_angle_);
        break;
    }
    case Drag::none:
        break;
    }
    return true;
}

void LiveImageOverlay::draw(ImDrawList* list, const ImVec2 surface_min,
                           const ImVec2 surface_size) const {
    if (!loaded())
        return;

    const ImVec2 center_px(surface_min.x + center_.x * surface_size.x,
                           surface_min.y + center_.y * surface_size.y);
    const float half_w = half_width_frac_ * surface_size.x;
    const float half_h = half_w * (static_cast<float>(pixel_height_) /
                                   static_cast<float>(std::max(pixel_width_, 1)));
    const float cosr = std::cos(rotation_);
    const float sinr = std::sin(rotation_);
    const auto to_screen = [&](const ImVec2 local) {
        return ImVec2(center_px.x + local.x * cosr - local.y * sinr,
                      center_px.y + local.x * sinr + local.y * cosr);
    };
    const ImVec2 tl = to_screen(ImVec2(-half_w, -half_h));
    const ImVec2 tr = to_screen(ImVec2(half_w, -half_h));
    const ImVec2 br = to_screen(ImVec2(half_w, half_h));
    const ImVec2 bl = to_screen(ImVec2(-half_w, half_h));

    const ImU32 tint = IM_COL32(255, 255, 255, static_cast<int>(alpha_ * 255.0f + 0.5f));
    list->AddImageQuad(texture_, tl, tr, br, bl, ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1),
                       ImVec2(0, 1), tint);
    if (locked)
        return;

    const ImU32 outline = IM_COL32(255, 255, 255, 210);
    const ImU32 handle_fill = IM_COL32(64, 170, 255, 255);
    list->AddQuad(tl, tr, br, bl, outline, px(1.5f));

    const ImVec2 resize_handle = to_screen(ImVec2(half_w, half_h));
    const ImVec2 rotate_handle = to_screen(ImVec2(0.0f, -half_h - px(rotate_handle_gap)));
    list->AddLine(to_screen(ImVec2(0.0f, -half_h)), rotate_handle, outline, px(1.5f));

    const float handle_radius = px(7.0f);
    list->AddCircleFilled(resize_handle, handle_radius, handle_fill, 16);
    list->AddCircle(resize_handle, handle_radius, outline, 16, px(1.5f));
    list->AddCircleFilled(rotate_handle, handle_radius, handle_fill, 16);
    list->AddCircle(rotate_handle, handle_radius, outline, 16, px(1.5f));
}

} // namespace gui
