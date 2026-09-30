// SPDX-License-Identifier: MIT
#include "control_api.hpp"

#include "aoahid_player/device.hpp"
#include "aoahid_player/event_script.hpp"
#include "aoahid_player/key_names.hpp"
#include "aoahid_player/paths.hpp"
#include "aoahid_player/spec_builder.hpp"

// cpp-httplib is vendored (see CMakeLists.txt's FetchContent block) purely
// for its HTTP parsing; every optional backend (TLS, compression) is turned
// off there, so this is the only place httplib.h is ever included.
#include <httplib.h>

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace gui {
namespace {

// --- Tiny JSON writer -------------------------------------------------------
// No parser is needed (every request reads as query parameters instead, via
// cpp-httplib's own decoding — see register_routes()), so a hand-written
// encoder is simpler than adding a JSON library for this alone.

std::string json_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

// A tiny append-only JSON object builder. Every value writer takes the key
// itself, so callers never have to balance quotes or commas by hand.
class JsonWriter {
  public:
    JsonWriter& str(const char* key, const std::string& value) {
        field(key);
        text_ += '"';
        text_ += json_escape(value);
        text_ += '"';
        return *this;
    }
    JsonWriter& str(const char* key, const char* value) { return str(key, std::string(value)); }
    JsonWriter& num(const char* key, const int64_t value) {
        field(key);
        text_ += std::to_string(value);
        return *this;
    }
    JsonWriter& boolean(const char* key, const bool value) {
        field(key);
        text_ += value ? "true" : "false";
        return *this;
    }
    // A pre-built JSON value (object, array, or a nested writer's str()) —
    // for the one or two levels of nesting these responses need.
    JsonWriter& raw(const char* key, const std::string& json) {
        field(key);
        text_ += json;
        return *this;
    }

    [[nodiscard]] std::string done() {
        text_ += '}';
        return std::move(text_);
    }

  private:
    void field(const char* key) {
        if (!first_)
            text_ += ',';
        first_ = false;
        text_ += '"';
        text_ += key;
        text_ += "\":";
    }

    std::string text_{"{"};
    bool first_{true};
};

std::string json_error(const std::string& message) {
    JsonWriter writer;
    return writer.boolean("ok", false).str("error", message).done();
}

std::string json_ok() {
    JsonWriter writer;
    return writer.boolean("ok", true).done();
}

// --- Query parameter parsing -------------------------------------------------

bool parse_bool(const std::string& text, const bool default_value) noexcept {
    if (text == "1" || text == "true" || text == "on" || text == "yes")
        return true;
    if (text == "0" || text == "false" || text == "off" || text == "no")
        return false;
    return default_value;
}

// Accepts decimal or a "0x"/"0" prefix (matching how the GUI's own hex
// fields, e.g. keyboard usages, are typically written).
bool parse_int(const std::string& text, int64_t& out) noexcept {
    if (text.empty())
        return false;
    char* end = nullptr;
    errno = 0;
    const long long value = std::strtoll(text.c_str(), &end, 0);
    if (end != text.c_str() + text.size() || errno == ERANGE)
        return false;
    out = value;
    return true;
}

// A usage number, or a name looked up by `from_name` (the CSV names).
bool parse_usage(const std::string& text, bool (*from_name)(std::string_view, uint16_t&) noexcept,
                 uint16_t& out) noexcept {
    int64_t value = 0;
    if (parse_int(text, value)) {
        if (value < 0 || value > 0xFFFF)
            return false;
        out = static_cast<uint16_t>(value);
        return true;
    }
    return !text.empty() && from_name(text, out);
}

UiRequest ui_request(const UiRequest::Kind kind) {
    UiRequest request;
    request.kind = kind;
    return request;
}

// "all", or 1-based numbers such as "1,3" (the Devices card's order).
bool parse_device_list(const std::string& text, std::vector<size_t>& out, bool& all) {
    out.clear();
    all = text == "all";
    if (all)
        return true;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = text.find(',', start);
        const std::string item =
            text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        int64_t number = 0;
        if (!parse_int(item, number) || number < 1)
            return false;
        out.push_back(static_cast<size_t>(number - 1));
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return !out.empty();
}

bool parse_int32(const std::string& text, int32_t& out) noexcept {
    int64_t value = 0;
    if (!parse_int(text, value) || value < INT32_MIN || value > INT32_MAX)
        return false;
    out = static_cast<int32_t>(value);
    return true;
}

const char* phase_name(const Engine::Phase phase) noexcept {
    switch (phase) {
    case Engine::Phase::idle:
        return "idle";
    case Engine::Phase::refreshing:
        return "refreshing";
    case Engine::Phase::connecting:
        return "connecting";
    case Engine::Phase::connected:
        return "connected";
    case Engine::Phase::playing:
        return "playing";
    case Engine::Phase::disconnecting:
        return "disconnecting";
    }
    return "idle";
}

const char* playback_state_name(const aoap::PlaybackState state) noexcept {
    switch (state) {
    case aoap::PlaybackState::stopped:
        return "stopped";
    case aoap::PlaybackState::playing:
        return "playing";
    case aoap::PlaybackState::paused:
        return "paused";
    }
    return "stopped";
}

// A bare name (no path separator) is looked up in the shared csv/ folder
// (see aoap::script_directory()), with ".csv" added if missing, the same as
// the GUI's own picker; anything else (absolute, or containing a slash) is
// used exactly as given.
std::string resolve_script_path(const std::string& script) {
    std::filesystem::path given = aoap::utf8_path(script);
    if (given.is_absolute() || given.has_parent_path())
        return script;
    given = aoap::utf8_path(aoap::script_directory()) / given;
    if (given.extension() != ".csv")
        given += ".csv";
    return aoap::path_utf8(given);
}

} // namespace

ControlApi::ControlApi(Engine& engine, UiHandler ui, const std::atomic<bool>& recording)
    : engine_(engine), ui_(std::move(ui)), recording_(recording) {}

ControlApi::~ControlApi() { stop(); }

int ControlApi::api_finger() const noexcept {
    const uint32_t contacts = engine_.connected_setup().touch.max_contacts;
    return contacts > 1 ? static_cast<int>(contacts) - 2 : 0;
}

std::string ControlApi::start(const int port) {
    if (running()) {
        if (port_ == port)
            return {};
        stop();
    }
    server_ = std::make_unique<httplib::Server>();
    // One waiting thread is enough for a local control surface; more start
    // only while several requests are in progress.
    server_->new_task_queue = [] { return new httplib::ThreadPool(1, 8); };
    // A browser names the page's own host in Host and adds Origin to a
    // cross-site request, so this keeps web pages (and DNS rebinding) out.
    server_->set_pre_routing_handler([port](const httplib::Request& req, httplib::Response& res) {
        std::string host = req.get_header_value("Host");
        for (char& c : host)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::string suffix = ":" + std::to_string(port);
        if (req.has_header("Origin") ||
            (host != "127.0.0.1" + suffix && host != "localhost" + suffix)) {
            res.status = 403;
            res.set_content(json_error("Requests from web pages are refused."),
                            "application/json");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });
    register_routes();
    // set_address_family is not needed: passing "127.0.0.1" already binds
    // that interface alone, never a public one.
    if (!server_->bind_to_port("127.0.0.1", port)) {
        server_.reset();
        return "Could not bind 127.0.0.1:" + std::to_string(port) +
               " (already in use, or not permitted).";
    }
    port_ = port;
    thread_ = std::thread([this] { server_->listen_after_bind(); });
    return {};
}

void ControlApi::stop() {
    if (!running())
        return;
    if (server_)
        server_->stop();
    thread_.join();
    server_.reset();
}

void ControlApi::register_routes() {
    using Req = httplib::Request;
    using Res = httplib::Response;
    httplib::Server& svr = *server_;

    svr.Get("/", [](const Req&, Res& res) {
        res.set_content(
            "aoahid_player control API. See README.md's \"Control API\" section for the "
            "full route list. Parameters go in the query string; GET /status reports the "
            "state, every other route is a POST, and each answers with a small JSON object.\n",
            "text/plain");
    });

    svr.Get("/status", [this](const Req&, Res& res) {
        const Engine::Phase phase = engine_.phase();
        const uint32_t profiles = engine_.connected_profiles();
        std::string profiles_json = "[";
        bool first = true;
        for (size_t index = 0; index < aoap::profile_count; ++index) {
            const auto profile = static_cast<aoap::Profile>(index);
            if ((profiles & aoap::profile_bit(profile)) == 0U)
                continue;
            if (!first)
                profiles_json += ',';
            first = false;
            profiles_json += '"';
            profiles_json += aoap::profile_name(profile);
            profiles_json += '"';
        }
        profiles_json += ']';

        const aoap::PlaybackStatus status = engine_.player().status();
        JsonWriter playback;
        playback.str("state", playback_state_name(status.state))
            .str("lap", status.position.lap == aoap::Lap::first ? "first" : "repeat")
            .num("time_ms", status.position.time_ns / 1'000'000)
            .num("loops", static_cast<int64_t>(status.loops))
            .num("reports", static_cast<int64_t>(status.reports));

        std::string available = "[";
        for (const aoap::DeviceEntry& entry : engine_.devices()) {
            if (available.size() > 1)
                available += ',';
            available += '"' + json_escape(entry.label) + '"';
        }
        available += ']';
        const std::vector<aoap::DeviceStatus> connected = engine_.device_status();
        std::string bridges = "[";
        for (const aoap::DeviceStatus& device : connected) {
            if (bridges.size() > 1)
                bridges += ',';
            bridges += std::to_string(device.adb_port);
        }
        bridges += ']';

        JsonWriter writer;
        writer.boolean("ok", true)
            .str("phase", phase_name(phase))
            .boolean("connected", engine_.connected())
            .num("devices", static_cast<int64_t>(connected.size()))
            .raw("available", available)
            .raw("profiles", profiles_json)
            .raw("bridges", bridges)
            .boolean("live_active", engine_.live_active())
            .boolean("recording", recording_.load(std::memory_order_relaxed))
            .raw("playback", playback.done());
        res.set_content(writer.done(), "application/json");
    });

    // --- Connection, recording, and the ADB Bridge: carried out by the UI
    // thread (see UiRequest), exactly as its buttons do. ----------------
    const auto run_ui = [this](const UiRequest& request, Res& res) {
        const UiReply reply = ui_(request);
        res.status = reply.status;
        res.set_content(reply.error.empty() ? json_ok() : json_error(reply.error),
                        "application/json");
    };
    svr.Post("/refresh", [run_ui](const Req&, Res& res) {
        run_ui(ui_request(UiRequest::Kind::refresh), res);
    });
    svr.Post("/connect", [run_ui](const Req& req, Res& res) {
        UiRequest request = ui_request(UiRequest::Kind::connect);
        if (req.has_param("devices") &&
            !parse_device_list(req.get_param_value("devices"), request.devices,
                               request.all_devices)) {
            res.status = 400;
            res.set_content(json_error("\"devices\" must be \"all\" or numbers such as 1,3."),
                            "application/json");
            return;
        }
        run_ui(request, res);
    });
    svr.Post("/disconnect", [this](const Req&, Res& res) {
        if (!engine_.connected()) {
            res.status = 409;
            res.set_content(json_error("Not connected."), "application/json");
            return;
        }
        engine_.disconnect();
        res.set_content(json_ok(), "application/json");
    });
    svr.Post("/record/start", [run_ui](const Req& req, Res& res) {
        UiRequest request = ui_request(UiRequest::Kind::record_start);
        request.name = req.get_param_value("name");
        run_ui(request, res);
    });
    svr.Post("/record/stop", [run_ui](const Req&, Res& res) {
        run_ui(ui_request(UiRequest::Kind::record_stop), res);
    });
    svr.Post("/bridge", [run_ui](const Req& req, Res& res) {
        UiRequest request = ui_request(UiRequest::Kind::bridge);
        int64_t device = 0;
        int64_t port = 0;
        if (!parse_int(req.get_param_value("device"), device) || device < 1 ||
            (req.has_param("port") &&
             (!parse_int(req.get_param_value("port"), port) || port < 1024 || port > 65535))) {
            res.status = 400;
            res.set_content(json_error("Missing or invalid \"device\" (1-based) or \"port\" "
                                       "(1024-65535)."),
                            "application/json");
            return;
        }
        request.device = static_cast<size_t>(device - 1);
        request.on = parse_bool(req.get_param_value("on"), true);
        request.port = static_cast<int>(port);
        run_ui(request, res);
    });

    // --- Playback ------------------------------------------------------
    svr.Post("/play", [this](const Req& req, Res& res) {
        // Engine::play() itself silently no-ops outside Phase::connected, so
        // this checks first — otherwise a caller sees "ok" for a script that
        // never started (not connected, still connecting, or already playing).
        if (engine_.phase() != Engine::Phase::connected) {
            res.status = 409;
            res.set_content(json_error("Not connected (or already playing)."),
                            "application/json");
            return;
        }
        const std::string script_param = req.get_param_value("script");
        if (script_param.empty()) {
            res.status = 400;
            res.set_content(json_error("Missing \"script\"."), "application/json");
            return;
        }
        auto script = std::make_shared<aoap::EventScript>();
        std::string error;
        if (!script->load(resolve_script_path(script_param), error)) {
            res.status = 400;
            res.set_content(json_error(error), "application/json");
            return;
        }
        int64_t loops = 0;
        parse_int(req.get_param_value("loop"), loops);
        engine_.play(script, aoap::display_name(script_param), {}, loops);
        res.set_content(json_ok(), "application/json");
    });
    svr.Post("/stop", [this](const Req&, Res& res) {
        engine_.stop();
        res.set_content(json_ok(), "application/json");
    });
    svr.Post("/pause", [this](const Req& req, Res& res) {
        if (engine_.phase() != Engine::Phase::playing) {
            res.status = 409;
            res.set_content(json_error("Nothing is playing."), "application/json");
            return;
        }
        if (parse_bool(req.get_param_value("paused"), true))
            engine_.player().pause();
        else
            engine_.player().resume();
        res.set_content(json_ok(), "application/json");
    });
    svr.Post("/seek", [this](const Req& req, Res& res) {
        if (engine_.phase() != Engine::Phase::playing) {
            res.status = 409;
            res.set_content(json_error("Nothing is playing."), "application/json");
            return;
        }
        int64_t time_ms = 0;
        if (!parse_int(req.get_param_value("time_ms"), time_ms) || time_ms < 0) {
            res.status = 400;
            res.set_content(json_error("Missing or invalid \"time_ms\"."), "application/json");
            return;
        }
        const aoap::Lap lap =
            req.get_param_value("lap") == "repeat" ? aoap::Lap::repeat : aoap::Lap::first;
        engine_.player().seek({lap, time_ms * 1'000'000});
        res.set_content(json_ok(), "application/json");
    });

    // --- Live control ----------------------------------------------------
    svr.Post("/live/start", [this](const Req&, Res& res) {
        // Same reasoning as /play: Engine::live_start() no-ops outside
        // Phase::connected/playing, so check first rather than report "ok"
        // for input that will not actually reach the device.
        if (!engine_.connected()) {
            res.status = 409;
            res.set_content(json_error("Not connected."), "application/json");
            return;
        }
        engine_.live_start();
        res.set_content(json_ok(), "application/json");
    });
    svr.Post("/live/stop", [this](const Req&, Res& res) {
        engine_.live_stop();
        res.set_content(json_ok(), "application/json");
    });

    // --- Input, forwarded exactly as Live control forwards it (see
    // Engine::live_send()); all require /live/start first, same as the
    // Live tab's own toggle. -------------------------------------------
    svr.Post("/touch", [this](const Req& req, Res& res) {
        int32_t x = 0;
        int32_t y = 0;
        if (!parse_int32(req.get_param_value("x"), x) ||
            !parse_int32(req.get_param_value("y"), y)) {
            res.status = 400;
            res.set_content(json_error("Missing or invalid \"x\"/\"y\"."), "application/json");
            return;
        }
        const bool state = parse_bool(req.get_param_value("state"), true);
        engine_.live_send(aoap::TouchEvent{api_finger(), state, x, y});
        res.set_content(json_ok(), "application/json");
    });
    svr.Post("/mouse/move", [this](const Req& req, Res& res) {
        int32_t dx = 0;
        int32_t dy = 0;
        parse_int32(req.get_param_value("dx"), dx);
        parse_int32(req.get_param_value("dy"), dy);
        engine_.live_send(aoap::MouseMove{dx, dy});
        res.set_content(json_ok(), "application/json");
    });
    svr.Post("/mouse/button", [this](const Req& req, Res& res) {
        int64_t button = 0;
        if (!parse_int(req.get_param_value("button"), button) || button < 1) {
            res.status = 400;
            res.set_content(json_error("Missing or invalid \"button\" (1-based)."),
                            "application/json");
            return;
        }
        engine_.live_send(
            aoap::MouseButton{static_cast<uint32_t>(button), parse_bool(req.get_param_value("down"), true)});
        res.set_content(json_ok(), "application/json");
    });
    svr.Post("/mouse/wheel", [this](const Req& req, Res& res) {
        int32_t delta = 0;
        parse_int32(req.get_param_value("delta"), delta);
        engine_.live_scroll(delta);
        res.set_content(json_ok(), "application/json");
    });
    svr.Post("/key", [this](const Req& req, Res& res) {
        uint16_t usage = 0;
        if (!parse_usage(req.get_param_value("usage"), aoap::key_usage_from_name, usage)) {
            res.status = 400;
            res.set_content(
                json_error("Missing or invalid \"usage\" (a HID keyboard usage or key name)."),
                "application/json");
            return;
        }
        engine_.live_send(aoap::KeyEvent{usage, parse_bool(req.get_param_value("down"), true)});
        res.set_content(json_ok(), "application/json");
    });
    svr.Post("/gamepad/button", [this](const Req& req, Res& res) {
        int64_t index = 0;
        if (!parse_int(req.get_param_value("index"), index) || index < 1) {
            res.status = 400;
            res.set_content(json_error("Missing or invalid \"index\" (1-based)."),
                            "application/json");
            return;
        }
        engine_.live_send(aoap::GamepadButton{static_cast<uint32_t>(index),
                                              parse_bool(req.get_param_value("down"), true)});
        res.set_content(json_ok(), "application/json");
    });
    svr.Post("/gamepad/axis", [this](const Req& req, Res& res) {
        int64_t index = 0;
        int32_t value = 0;
        if (!parse_int(req.get_param_value("index"), index) || index < 0 ||
            !parse_int32(req.get_param_value("value"), value)) {
            res.status = 400;
            res.set_content(json_error("Missing or invalid \"index\"/\"value\"."),
                            "application/json");
            return;
        }
        engine_.live_send(aoap::GamepadAxis{static_cast<size_t>(index), value});
        res.set_content(json_ok(), "application/json");
    });
    svr.Post("/gamepad/dpad", [this](const Req& req, Res& res) {
        engine_.live_send(aoap::GamepadDpad{parse_bool(req.get_param_value("up"), false),
                                            parse_bool(req.get_param_value("down"), false),
                                            parse_bool(req.get_param_value("right"), false),
                                            parse_bool(req.get_param_value("left"), false)});
        res.set_content(json_ok(), "application/json");
    });
    svr.Post("/media", [this](const Req& req, Res& res) {
        uint16_t usage = 0;
        if (!parse_usage(req.get_param_value("key"), aoap::media_usage_from_name, usage) ||
            !aoap::media_usage_supported(usage)) {
            res.status = 400;
            res.set_content(json_error("Missing or invalid \"key\" (a media key name or one "
                                       "of the toggle profile's usages)."),
                            "application/json");
            return;
        }
        // Without "down" it is a tap: press, then release in the next report.
        if (req.has_param("down")) {
            engine_.live_send(aoap::MediaKey{usage, parse_bool(req.get_param_value("down"), true)});
        } else {
            engine_.live_send(aoap::MediaKey{usage, true});
            engine_.live_send(aoap::MediaKey{usage, false});
        }
        res.set_content(json_ok(), "application/json");
    });
    svr.Post("/pen", [this](const Req& req, Res& res) {
        int32_t x = 0;
        int32_t y = 0;
        int32_t pressure = 0;
        if (!parse_int32(req.get_param_value("x"), x) ||
            !parse_int32(req.get_param_value("y"), y) ||
            (req.has_param("pressure") &&
             !parse_int32(req.get_param_value("pressure"), pressure))) {
            res.status = 400;
            res.set_content(json_error("Missing or invalid \"x\"/\"y\"/\"pressure\"."),
                            "application/json");
            return;
        }
        const bool in_range = parse_bool(req.get_param_value("in_range"), true);
        const bool tip = parse_bool(req.get_param_value("tip"), false);
        if (tip && !in_range) {
            res.status = 400;
            res.set_content(json_error("\"tip\" needs \"in_range\"."), "application/json");
            return;
        }
        engine_.live_send(aoap::PenSample{in_range, tip, x, y, pressure});
        res.set_content(json_ok(), "application/json");
    });
}

} // namespace gui
