// SPDX-License-Identifier: MIT
#pragma once

#include "engine.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace httplib {
class Server;
} // namespace httplib

namespace gui {

// A request only the UI thread can carry out, because it owns the device
// selection, the profile settings, the recorder, and the ADB Bridge rows.
struct UiRequest {
    enum class Kind : uint8_t { refresh, connect, record_start, record_stop, bridge };
    Kind kind{Kind::refresh};
    std::vector<size_t> devices; // connect: indices into the device list; empty = keep
    bool all_devices{};          // connect: every listed device
    std::string name;            // record_start: file name, empty = timestamped
    size_t device{};             // bridge: index into the connected devices
    bool on{};                   // bridge
    int port{};                  // bridge: 0 = the port the ADB tab shows
};

struct UiReply {
    int status{200};
    std::string error; // empty on success
};

// Runs `request` on the UI thread and waits for its reply.
using UiHandler = std::function<UiReply(const UiRequest&)>;

// A small local HTTP control surface for driving the connected phone from
// another program: refresh, connect, and disconnect; play/stop/seek a script;
// forward touch/mouse/keyboard/gamepad/pen/media-key input; record; switch
// ADB Bridges; and read status. Input and playback routes are thin
// translations into the same Engine calls the UI itself makes (already safe
// to call from any thread — see engine.hpp). Routes that need the UI's own
// state go through `ui` (a UiRequest the UI thread runs on its next frame),
// so they behave exactly like the matching buttons.
//
// Off by default (see Settings::api_enabled) and binds 127.0.0.1 only, never
// a public interface: any local program can reach it once it is on, with no
// further authentication, so turning it on is a deliberate choice. Requests
// from web pages (an Origin header, or a Host other than this address) are
// refused, so a page open in a browser cannot drive the phone. Every request
// carries its parameters in the query string (GET / and GET /status, POST
// for the rest), and responses are small hand-written JSON, so this needs no
// JSON library. See register_routes() for the full list of paths, or GET /
// for a plain-text summary of them.
class ControlApi {
  public:
    // `recording` mirrors whether the Recorder is running; the UI thread
    // keeps it current.
    ControlApi(Engine& engine, UiHandler ui, const std::atomic<bool>& recording);
    ~ControlApi();

    ControlApi(const ControlApi&) = delete;
    ControlApi& operator=(const ControlApi&) = delete;

    // Starts listening on 127.0.0.1:`port` on its own thread; a no-op if
    // already running on that port, else restarts on the new one. Returns
    // an error message on failure (e.g. the port is already taken), empty
    // on success.
    std::string start(int port);
    // Stops listening and joins the thread; a no-op if not running.
    void stop();
    [[nodiscard]] bool running() const noexcept { return thread_.joinable(); }
    [[nodiscard]] int port() const noexcept { return port_; }

  private:
    void register_routes();
    // The touch contact slot the API uses: the second-to-last declared
    // contact, one below the Live tab's own (see App::live_finger()), so a
    // program driving the API and a person using the Live tab can both
    // touch at once without colliding.
    [[nodiscard]] int api_finger() const noexcept;

    Engine& engine_;
    UiHandler ui_;
    const std::atomic<bool>& recording_;
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    int port_{};
};

} // namespace gui
