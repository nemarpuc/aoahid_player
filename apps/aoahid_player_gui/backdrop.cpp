// SPDX-License-Identifier: MIT
#include "backdrop.hpp"

#include "live_image.hpp"
#include "theme.hpp"

#include <imgui_impl_opengl3_loader.h>
#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703 // core since OpenGL 1.0; imgui's loader omits it
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace gui::backdrop {
namespace {

// Larger pictures are scaled down before upload, so a 6K photo never costs
// more texture memory than a 2560px one.
constexpr int max_side = 2560;
// The blurred copy is this small; the GPU's linear filtering smooths it back
// up, and blurring it is still cheap enough to redo on every slider step.
constexpr int blur_side = 768;
// The window width the blur radius is meant for; the radius in the small
// copy is scaled from it.
constexpr float reference_width = 1280.0f;

struct State {
    GLuint full{};
    GLuint blurred{};
    int width{};
    int height{};
    std::vector<unsigned char> reduced; // RGBA, blur_side on its long edge
    int small_width{};
    int small_height{};
    int blur{6};
    std::string path;
    // Where draw() put the picture this frame, for draw_frosted().
    bool shown{};
    ImVec2 min{};
    ImVec2 size{1, 1};
    ImVec2 uv0{};
    ImVec2 uv1{1, 1};
    ImU32 dim{};
    ImU32 color{}; // what draw() filled the window with when there is no picture
};

State& state() {
    static State instance;
    return instance;
}

size_t offset(const int x, const int y, const int width) {
    return (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
}

// value * numerator / denominator without overflowing int on a very long,
// thin picture.
int scaled(const int value, const int numerator, const int denominator) {
    return static_cast<int>(static_cast<int64_t>(value) * numerator / denominator);
}

// Area average: every source pixel lands in exactly one target pixel.
std::vector<unsigned char> shrink(const unsigned char* source, const int width,
                                  const int height, const int target_width,
                                  const int target_height) {
    std::vector<unsigned char> target(static_cast<size_t>(target_width) *
                                      static_cast<size_t>(target_height) * 4);
    for (int ty = 0; ty < target_height; ++ty) {
        const int y0 = scaled(ty, height, target_height);
        const int y1 = std::max(y0 + 1, scaled(ty + 1, height, target_height));
        for (int tx = 0; tx < target_width; ++tx) {
            const int x0 = scaled(tx, width, target_width);
            const int x1 = std::max(x0 + 1, scaled(tx + 1, width, target_width));
            unsigned sum[4]{};
            for (int y = y0; y < y1; ++y) {
                const unsigned char* pixel = &source[offset(x0, y, width)];
                for (int x = x0; x < x1; ++x, pixel += 4)
                    for (size_t c = 0; c < 4; ++c)
                        sum[c] += pixel[c];
            }
            const auto count = static_cast<unsigned>((y1 - y0) * (x1 - x0));
            unsigned char* out = &target[offset(tx, ty, target_width)];
            for (size_t c = 0; c < 4; ++c)
                out[c] = static_cast<unsigned char>(sum[c] / count);
        }
    }
    return target;
}

// One box blur pass over `lines` lines of `length` pixels; `line_step` and
// `step` are byte strides, so the same pass runs along rows or columns.
// Edges repeat the outermost pixel.
void box_pass(const std::vector<unsigned char>& in, std::vector<unsigned char>& out,
              const int lines, const int length, const size_t line_step, const size_t step,
              const int radius) {
    const auto window = static_cast<unsigned>(radius * 2 + 1);
    for (int line = 0; line < lines; ++line) {
        const size_t base = static_cast<size_t>(line) * line_step;
        const auto at = [&](const int index) {
            return base + static_cast<size_t>(std::clamp(index, 0, length - 1)) * step;
        };
        for (size_t c = 0; c < 4; ++c) {
            unsigned sum = 0;
            for (int k = -radius; k <= radius; ++k)
                sum += in[at(k) + c];
            for (int i = 0; i < length; ++i) {
                out[at(i) + c] = static_cast<unsigned char>(sum / window);
                sum += in[at(i + radius + 1) + c];
                sum -= in[at(i - radius) + c];
            }
        }
    }
}

GLuint upload(const unsigned char* pixels, const int width, const int height, GLuint texture) {
    if (texture == 0)
        glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

// The picture with every half-size level below it, so the GPU shrinks it
// to the window smoothly instead of skipping pixels.
GLuint upload_mipmapped(const unsigned char* pixels, int width, int height) {
    std::vector<unsigned char> level_pixels;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    for (GLint level = 0;; ++level) {
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     pixels);
        if (width == 1 && height == 1)
            break;
        const int next_width = std::max(1, width / 2);
        const int next_height = std::max(1, height / 2);
        level_pixels = shrink(pixels, width, height, next_width, next_height);
        pixels = level_pixels.data();
        width = next_width;
        height = next_height;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

// Three box passes each way come close to a Gaussian.
void make_blurred() {
    State& s = state();
    if (s.reduced.empty())
        return;
    const auto radius = static_cast<int>(std::lround(
        static_cast<float>(s.blur) * static_cast<float>(blur_side) / reference_width));
    std::vector<unsigned char> image = s.reduced;
    if (radius > 0) {
        std::vector<unsigned char> scratch(image.size());
        const size_t row = static_cast<size_t>(s.small_width) * 4;
        for (int pass = 0; pass < 3; ++pass) {
            box_pass(image, scratch, s.small_height, s.small_width, row, 4, radius);
            box_pass(scratch, image, s.small_width, s.small_height, 4, row, radius);
        }
    }
    // Frosted glass reads livelier with its colours pushed a little.
    for (size_t index = 0; index + 3 < image.size(); index += 4) {
        const int luma = (image[index] * 54 + image[index + 1] * 183 + image[index + 2] * 19) >> 8;
        for (size_t c = 0; c < 3; ++c) {
            const int boosted = luma + (static_cast<int>(image[index + c]) - luma) * 3 / 2;
            image[index + c] = static_cast<unsigned char>(std::clamp(boosted, 0, 255));
        }
    }
    s.blurred = upload(image.data(), s.small_width, s.small_height, s.blurred);
}

std::pair<int, int> fit(const int width, const int height, const int side) {
    if (width <= side && height <= side)
        return {width, height};
    if (width >= height)
        return {side, std::max(1, scaled(height, side, width))};
    return {std::max(1, scaled(width, side, height)), side};
}

} // namespace

std::string load(const std::string& file) {
    DecodedImage image;
    std::string error = decode_image(file, image);
    if (!error.empty())
        return error;
    const auto [full_width, full_height] = fit(image.width, image.height, max_side);
    std::vector<unsigned char> shrunk;
    const unsigned char* pixels = image.pixels.get();
    if (full_width != image.width || full_height != image.height) {
        shrunk = shrink(pixels, image.width, image.height, full_width, full_height);
        image.pixels.reset();
        pixels = shrunk.data();
    }

    State& s = state();
    const auto [small_width, small_height] = fit(full_width, full_height, blur_side);
    s.reduced = shrink(pixels, full_width, full_height, small_width, small_height);
    if (s.full != 0)
        glDeleteTextures(1, &s.full);
    s.full = upload_mipmapped(pixels, full_width, full_height);
    s.width = full_width;
    s.height = full_height;
    s.small_width = small_width;
    s.small_height = small_height;
    s.path = file;
    if (theme::glass_enabled())
        make_blurred();
    else if (s.blurred != 0) {
        glDeleteTextures(1, &s.blurred);
        s.blurred = 0;
    }
    return {};
}

void clear() {
    State& s = state();
    if (s.full != 0)
        glDeleteTextures(1, &s.full);
    if (s.blurred != 0)
        glDeleteTextures(1, &s.blurred);
    const int blur = s.blur;
    s = State{};
    s.blur = blur;
}

bool loaded() { return state().full != 0; }

const std::string& path() { return state().path; }

void set_blur(const int radius) {
    State& s = state();
    const int clamped = std::clamp(radius, 0, 40);
    if (clamped == s.blur)
        return;
    s.blur = clamped;
    if (theme::glass_enabled())
        make_blurred();
    else if (s.blurred != 0) {
        glDeleteTextures(1, &s.blurred);
        s.blurred = 0;
    }
}

void draw(ImDrawList* list, const ImVec2 min, const ImVec2 max, const bool image, const ImU32 color,
          const float dim) {
    State& s = state();
    if (!theme::glass_enabled() && s.blurred != 0) {
        glDeleteTextures(1, &s.blurred);
        s.blurred = 0;
    }
    s.shown = image && s.full != 0;
    s.color = color;
    if (!s.shown) {
        list->AddRectFilled(min, max, color);
        return;
    }
    // Cover: scaled to fill the window, centred, the overflow cut off.
    const ImVec2 size(std::max(max.x - min.x, 1.0f), std::max(max.y - min.y, 1.0f));
    const float scale = std::max(size.x / static_cast<float>(s.width),
                                 size.y / static_cast<float>(s.height));
    const float visible_u = size.x / (static_cast<float>(s.width) * scale);
    const float visible_v = size.y / (static_cast<float>(s.height) * scale);
    s.min = min;
    s.size = size;
    s.uv0 = ImVec2(0.5f - visible_u * 0.5f, 0.5f - visible_v * 0.5f);
    s.uv1 = ImVec2(0.5f + visible_u * 0.5f, 0.5f + visible_v * 0.5f);
    s.dim = theme::rgb(theme::dim_base,
                       static_cast<unsigned>(std::lround(std::clamp(dim, 0.0f, 1.0f) * 255.0f)));
    list->AddImage(static_cast<ImTextureID>(s.full), min, max, s.uv0, s.uv1);
    list->AddRectFilled(min, max, s.dim);
}

void draw_frosted(ImDrawList* list, const ImVec2 p0, const ImVec2 p1, const float rounding) {
    State& s = state();
    if (!s.shown)
        return;
    if (s.blurred == 0 && !s.reduced.empty())
        make_blurred();
    if (s.blurred == 0)
        return;
    // With no blur the glass shows the picture itself, sharp.
    const GLuint texture = s.blur == 0 ? s.full : s.blurred;
    const auto uv = [&](const ImVec2 p) {
        return ImVec2(s.uv0.x + (p.x - s.min.x) / s.size.x * (s.uv1.x - s.uv0.x),
                      s.uv0.y + (p.y - s.min.y) / s.size.y * (s.uv1.y - s.uv0.y));
    };
    // Through GetColorU32() so a fading parent (a notice, a new tab) fades this too.
    list->AddImageRounded(static_cast<ImTextureID>(texture), p0, p1, uv(p0), uv(p1),
                          ImGui::GetColorU32(IM_COL32_WHITE), rounding);
    list->AddRectFilled(p0, p1, ImGui::GetColorU32(s.dim), rounding);
}

void draw_base(ImDrawList* list, const ImVec2 p0, const ImVec2 p1, const float rounding) {
    const State& s = state();
    if (s.shown && s.blurred != 0)
        draw_frosted(list, p0, p1, rounding);
    else
        list->AddRectFilled(p0, p1, s.color, rounding);
}

} // namespace gui::backdrop
