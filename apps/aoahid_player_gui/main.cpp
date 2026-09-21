// SPDX-License-Identifier: MIT
//
// aoahid_player_gui — Dear ImGui front end. The window is redrawn only when
// something happens (input, a worker finishing, a status change), so an idle
// window costs no CPU; while playback or a recording is running it redraws at
// a modest fixed rate to animate progress.

#include "app.hpp"
#include "theme.hpp"

#include "aoahid_player/timing.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_opengl3_loader.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

constexpr double tick_seconds = 1.0 / 30.0;      // redraw rate while something animates
constexpr double hover_tick_window = 1.0;        // keep ticking briefly for tooltips
constexpr int64_t wayland_frame_ns = 8'333'333;  // frame cap where vsync is not used

std::string g_glfw_error;
gui::App* g_app = nullptr;
std::atomic<bool> g_scale_changed{false};
// Bumped by every input callback and every worker wake-up. A change means the
// next frames must be drawn even if the event itself was consumed while
// polling, so layout that lags one frame (auto-sized cards) always settles.
std::atomic<uint32_t> g_activity{0};

void bump() noexcept { g_activity.fetch_add(1, std::memory_order_relaxed); }

void fatal(const std::string& text) {
#if defined(_WIN32)
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<size_t>(std::max(length, 1)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length);
    MessageBoxW(nullptr, wide.c_str(), L"AOA HID Player", MB_OK | MB_ICONERROR);
#else
    std::fprintf(stderr, "aoahid_player_gui: %s\n", text.c_str());
#endif
}

void on_glfw_error(int, const char* description) {
    g_glfw_error = description != nullptr ? description : "";
}

void on_drop(GLFWwindow*, const int count, const char** paths) {
    bump();
    if (g_app == nullptr || count <= 0)
        return;
    std::vector<std::string> list;
    list.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index)
        list.emplace_back(paths[index]);
    g_app->on_drop(std::move(list));
}

void on_focus(GLFWwindow*, const int focused) {
    bump();
    if (g_app == nullptr)
        return;
    if (focused == GLFW_TRUE)
        g_app->on_focus();
    else
        g_app->on_focus_lost();
}

void on_content_scale(GLFWwindow*, float, float) {
    g_scale_changed.store(true);
    bump();
}

void on_cursor(GLFWwindow*, const double x, const double y) {
    bump();
    if (g_app != nullptr)
        g_app->on_cursor(x, y);
}
void on_button(GLFWwindow*, int, int, int) { bump(); }
void on_scroll(GLFWwindow*, double, double) { bump(); }
void on_key(GLFWwindow*, const int key, const int scancode, const int action, const int mods) {
    bump();
    if (g_app == nullptr)
        return;
    if (action == GLFW_PRESS && key == GLFW_KEY_F && (mods & GLFW_MOD_CONTROL) != 0) {
        g_app->on_search_shortcut();
        return;
    }
    if (action == GLFW_PRESS && key == GLFW_KEY_V &&
        (mods & (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT)) == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT)) {
        g_app->on_paste_shortcut();
        return;
    }
    if (action != GLFW_REPEAT)
        g_app->on_key(key, scancode, action == GLFW_PRESS);
}
void on_char(GLFWwindow*, unsigned int) { bump(); }
void on_refresh(GLFWwindow*) { bump(); }
void on_size(GLFWwindow*, int, int) { bump(); }

// Where the compositor scales the framebuffer (Wayland, macOS) the UI is laid
// out at 1.0 and ImGui renders at the framebuffer scale; elsewhere the style
// and fonts follow the monitor's content scale.
bool framebuffer_scaled() {
#if defined(__APPLE__)
    return true;
#else
    return glfwGetPlatform() == GLFW_PLATFORM_WAYLAND;
#endif
}

float monitor_scale(GLFWmonitor* monitor) {
    if (monitor == nullptr || framebuffer_scaled())
        return 1.0f;
    float x = 1.0f;
    float y = 1.0f;
    glfwGetMonitorContentScale(monitor, &x, &y);
    return std::clamp(x, 1.0f, 4.0f);
}

float window_scale(GLFWwindow* window) {
    if (framebuffer_scaled())
        return 1.0f;
    float x = 1.0f;
    float y = 1.0f;
    glfwGetWindowContentScale(window, &x, &y);
    return std::clamp(x, 1.0f, 4.0f);
}

// The monitor the window is mostly over, so entering full screen lands on
// the screen the window was already on rather than always the primary one.
GLFWmonitor* monitor_for_window(GLFWwindow* window) {
    int window_x = 0;
    int window_y = 0;
    int window_width = 0;
    int window_height = 0;
    glfwGetWindowPos(window, &window_x, &window_y);
    glfwGetWindowSize(window, &window_width, &window_height);
    const int centre_x = window_x + window_width / 2;
    const int centre_y = window_y + window_height / 2;

    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    for (int index = 0; index < count; ++index) {
        int area_x = 0;
        int area_y = 0;
        int area_width = 0;
        int area_height = 0;
        glfwGetMonitorWorkarea(monitors[index], &area_x, &area_y, &area_width, &area_height);
        if (centre_x >= area_x && centre_x < area_x + area_width && centre_y >= area_y &&
            centre_y < area_y + area_height)
            return monitors[index];
    }
    return glfwGetPrimaryMonitor();
}

// GLFW's Wayland backend has no input method support, so Japanese (or any
// IME) text cannot be typed into a native Wayland window. Under a Wayland
// session the window is opened on X11 (XWayland) instead, where GLFW talks
// XIM to fcitx5/ibus. Returns false when that is not possible.
bool prefer_x11_for_ime() {
#if defined(__linux__)
    if (std::getenv("WAYLAND_DISPLAY") == nullptr || std::getenv("DISPLAY") == nullptr ||
        glfwPlatformSupported(GLFW_PLATFORM_X11) != GLFW_TRUE)
        return false;
    if (std::getenv("XMODIFIERS") == nullptr) {
        const char* const ibus = std::getenv("GTK_IM_MODULE");
        setenv("XMODIFIERS", ibus != nullptr && std::string(ibus) == "ibus" ? "@im=ibus" : "@im=fcitx", 0);
    }
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    return true;
#else
    return false;
#endif
}

int run() {
    glfwSetErrorCallback(on_glfw_error);
    const bool forced_x11 = prefer_x11_for_ime();
    bool initialised = glfwInit() == GLFW_TRUE;
    if (!initialised && forced_x11) {
        // X11 was not reachable after all; let GLFW pick (native Wayland).
        glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
        g_glfw_error.clear();
        initialised = glfwInit() == GLFW_TRUE;
    }
    if (!initialised) {
        fatal("The window system could not be initialised." +
              (g_glfw_error.empty() ? std::string() : "\n\n" + g_glfw_error));
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);

    // Read once, directly, before the window (and so the App that would
    // normally own this) exists. main() also writes this same geometry back
    // each frame via App::set_window_geometry() so the App's own
    // persist_settings() carries it forward like every other setting.
    const gui::Settings startup_settings = gui::load_settings(gui::settings_path());

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    float scale = monitor_scale(monitor);
    int width = static_cast<int>(1360 * scale);
    int height = static_cast<int>(900 * scale);
    if (monitor != nullptr) {
        int x = 0;
        int y = 0;
        int work_width = 0;
        int work_height = 0;
        glfwGetMonitorWorkarea(monitor, &x, &y, &work_width, &work_height);
        if (work_width > 0 && work_height > 0) {
            width = std::min(width, static_cast<int>(work_width * 0.92));
            height = std::min(height, static_cast<int>(work_height * 0.92));
        }
    }
    if (startup_settings.window_width > 0 && startup_settings.window_height > 0) {
        width = startup_settings.window_width;
        height = startup_settings.window_height;
    }

    GLFWwindow* window = glfwCreateWindow(width, height, "AOA HID Player", nullptr, nullptr);
    if (window == nullptr) {
        fatal("The window could not be created. OpenGL 3.0 or newer is required; updating the "
              "graphics driver usually fixes this." +
              (g_glfw_error.empty() ? std::string() : "\n\n" + g_glfw_error));
        glfwTerminate();
        return 1;
    }
    glfwSetWindowSizeLimits(window, std::min(width, static_cast<int>(1000 * scale)),
                            std::min(height, static_cast<int>(640 * scale)), GLFW_DONT_CARE,
                            GLFW_DONT_CARE);
    if (startup_settings.window_width > 0 && startup_settings.window_height > 0) {
        // Position is inert on native Wayland: the protocol gives a client
        // no way to query or set its own absolute position, so GLFW's
        // glfwGetWindowPos()/glfwSetWindowPos() are no-ops there and this
        // block only ever restores 0,0 (harmless — width/height still
        // apply). Real on X11, XWayland, and Windows.
        // Only trusted if its centre still lands on some connected monitor;
        // a display that has since been unplugged should not strand the
        // window somewhere it can never be reached again.
        const int centre_x = startup_settings.window_x + startup_settings.window_width / 2;
        const int centre_y = startup_settings.window_y + startup_settings.window_height / 2;
        int monitor_count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&monitor_count);
        for (int index = 0; index < monitor_count; ++index) {
            int area_x = 0;
            int area_y = 0;
            int area_width = 0;
            int area_height = 0;
            glfwGetMonitorWorkarea(monitors[index], &area_x, &area_y, &area_width, &area_height);
            if (centre_x >= area_x && centre_x < area_x + area_width && centre_y >= area_y &&
                centre_y < area_y + area_height) {
                glfwSetWindowPos(window, startup_settings.window_x, startup_settings.window_y);
                break;
            }
        }
    }
    glfwMakeContextCurrent(window);

    // Wayland may block a vsync'd swap for as long as the window is hidden, so
    // there the frame rate is capped by hand instead.
    const bool wayland = glfwGetPlatform() == GLFW_PLATFORM_WAYLAND;
    glfwSwapInterval(wayland ? 0 : 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // nothing is saved between runs
    io.LogFilename = nullptr;
    io.ConfigInputTextCursorBlink = false; // a blinking caret would need constant redraws

    // Callbacks set before the backend installs its own are chained by it.
    glfwSetWindowFocusCallback(window, on_focus);
    glfwSetDropCallback(window, on_drop);
    glfwSetWindowContentScaleCallback(window, on_content_scale);
    glfwSetCursorPosCallback(window, on_cursor);
    glfwSetMouseButtonCallback(window, on_button);
    glfwSetScrollCallback(window, on_scroll);
    glfwSetKeyCallback(window, on_key);
    glfwSetCharCallback(window, on_char);
    glfwSetWindowRefreshCallback(window, on_refresh);
    glfwSetFramebufferSizeCallback(window, on_size);

    scale = window_scale(window);
    gui::theme::set_mode(startup_settings.dark_theme);
    gui::theme::setup(scale);
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    if (!ImGui_ImplOpenGL3_Init(nullptr)) {
        fatal("OpenGL could not be initialised. OpenGL 3.0 or newer is required.");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    auto app = std::make_unique<gui::App>([] {
        bump();
        glfwPostEmptyEvent();
    });
    g_app = app.get();

    int pending_frames = 2;
    bool shown = false;
    double last_motion = 0.0;
    bool tick_for_hover = false;
    int64_t last_frame = 0;
    uint32_t seen_activity = g_activity.load(std::memory_order_relaxed);

    // The Live tab's full screen mode is real OS-level exclusive full screen,
    // not just this window's own layout filling its (still windowed) frame.
    // The windowed geometry is remembered so leaving full screen restores
    // the exact position and size the window had before entering it.
    bool os_fullscreen = false;
    int windowed_x = 0;
    int windowed_y = 0;
    int windowed_width = width;
    int windowed_height = height;

    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        const bool want_fullscreen = g_app != nullptr && g_app->wants_os_fullscreen();
        if (want_fullscreen != os_fullscreen) {
            if (want_fullscreen) {
                glfwGetWindowPos(window, &windowed_x, &windowed_y);
                glfwGetWindowSize(window, &windowed_width, &windowed_height);
                if (wayland) {
                    // glfwGetWindowPos()/monitor_for_window() cannot work on
                    // native Wayland: the protocol gives a client no way to
                    // learn its own position or which output it is on, so
                    // asking GLFW for exclusive fullscreen on a specific
                    // GLFWmonitor* here would often land on the wrong
                    // screen. Maximizing a decoration-less window is a
                    // compositor-driven action that keeps it on whatever
                    // output it already occupies, which is what full screen
                    // should do.
                    glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
                    glfwMaximizeWindow(window);
                } else {
                    GLFWmonitor* target = monitor_for_window(window);
                    const GLFWvidmode* mode =
                        target != nullptr ? glfwGetVideoMode(target) : nullptr;
                    if (target != nullptr && mode != nullptr)
                        glfwSetWindowMonitor(window, target, 0, 0, mode->width, mode->height,
                                            mode->refreshRate);
                }
            } else if (wayland) {
                glfwRestoreWindow(window);
                glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
            } else {
                glfwSetWindowMonitor(window, nullptr, windowed_x, windowed_y, windowed_width,
                                    windowed_height, 0);
            }
            os_fullscreen = want_fullscreen;
            bump();
            pending_frames = std::max(pending_frames, 2);
        }
        if (pending_frames > 0) {
            glfwPollEvents();
            --pending_frames;
        } else if (app->animating() || tick_for_hover) {
            glfwWaitEventsTimeout(tick_seconds);
        } else {
            glfwWaitEvents();
        }
        const uint32_t activity = g_activity.load(std::memory_order_relaxed);
        if (activity != seen_activity) {
            // ImGui settles hover, clicks, and auto-sized layout over a
            // couple of frames after anything changes.
            seen_activity = activity;
            pending_frames = std::max(pending_frames, 2);
        }
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE) {
            glfwWaitEventsTimeout(0.25);
            continue;
        }
        // Reported every frame so App's own persist_settings() carries the
        // latest windowed geometry forward like any other setting; skipped
        // while OS-fullscreen since that geometry is a monitor filling, not
        // where the window should reappear windowed next run.
        if (!os_fullscreen) {
            int win_x = 0;
            int win_y = 0;
            int win_width = 0;
            int win_height = 0;
            glfwGetWindowPos(window, &win_x, &win_y);
            glfwGetWindowSize(window, &win_width, &win_height);
            app->set_window_geometry(win_x, win_y, win_width, win_height);
        }
        if (wayland) {
            const int64_t since = aoap::Timing::now_ns() - last_frame;
            if (since < wayland_frame_ns)
                std::this_thread::sleep_for(std::chrono::nanoseconds(wayland_frame_ns - since));
        }
        if (g_scale_changed.exchange(false)) {
            scale = window_scale(window);
            gui::theme::apply_style(scale);
        }
        if (app->consume_theme_change())
            gui::theme::apply_style(scale);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        app->frame();
        ImGui::Render();

        int framebuffer_width = 0;
        int framebuffer_height = 0;
        glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
        glViewport(0, 0, framebuffer_width, framebuffer_height);
        // Read fresh each frame (not hoisted above the loop): the theme
        // toggle changes `background` at any time, and glClearColor must
        // never paint one frame's edges in the other theme's colour.
        const ImVec4 clear = gui::theme::vec(gui::theme::background);
        glClearColor(clear.x, clear.y, clear.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
        last_frame = aoap::Timing::now_ns();

        const double now = ImGui::GetTime();
        if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)
            last_motion = now;
        tick_for_hover = ImGui::IsAnyItemHovered() && now - last_motion < hover_tick_window;

        if (!shown) {
            // Shown after the first frame so the window never flashes white.
            glfwShowWindow(window);
            shown = true;
            pending_frames = 2;
        }
    }

    // Workers may still wake the window while they wind down, so the App goes
    // first and GLFW last.
    g_app = nullptr;
    glfwHideWindow(window);
    app.reset();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        fatal(std::string("Unexpected error: ") + error.what());
        return 1;
    } catch (...) {
        fatal("Unexpected error.");
        return 1;
    }
}
