// SPDX-License-Identifier: MIT
#pragma once

#include "engine.hpp"

#include <memory>
#include <string>
#include <thread>

namespace httplib {
class Server;
} // namespace httplib

namespace gui {

// A small local HTTP control surface for driving the connected phone from
// another program: play/stop/seek a script, forward touch/mouse/keyboard/
// gamepad/media-key input, and read connection status. Every route is a
// thin translation into the same Engine calls the UI itself makes (already
// safe to call from any thread — see engine.hpp), so this adds no locking
// or state of its own beyond which port it listens on.
//
// Off by default (see Settings::api_enabled) and binds 127.0.0.1 only, never
// a public interface: any local program can reach it once it is on, with no
// further authentication, so turning it on is a deliberate choice. Query
// parameters carry every request (GET or POST, either works — cpp-httplib
// reads both the same way), and responses are small hand-written JSON, so
// this needs no JSON library. See control_api.cpp's route table for the
// full list of paths, or GET / for a plain-text summary of them.
class ControlApi {
  public:
    explicit ControlApi(Engine& engine);
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
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    int port_{};
};

} // namespace gui
