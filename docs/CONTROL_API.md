# Control API

A local HTTP server another program can use to drive the phone — the same
things the Devices tab, Live control, the Player tab, the Recorder, and the
ADB tab do, callable over plain HTTP. Off by default; turn it on with the
Settings tab's *Control API* switch (the *Port* it listens on, 47821 by
default, is next to it). It binds
`127.0.0.1` only, never a public interface, but has no further
authentication: any program running on this machine can reach it once it is
on, so turning it on is a deliberate choice, same as opening a debug port.

Every route takes plain query parameters — `curl -X POST
"http://127.0.0.1:47821/touch?x=500&y=900&state=true"` — with no request body
to build. Boolean parameters take `true`/`false`, `1`/`0`, `on`/`off`, or
`yes`/`no`; one that is left out takes the default in the table below, and
one that is given with any other value answers `400`. Numbers are decimal, or hex with a `0x` prefix (a leading zero
is still decimal, as in CSV scripts). `GET /` and `GET /status` are the only GET routes; every other
route is POST. A route answers `200` with a small JSON object, at least
`{"ok":true}` or `{"ok":false,"error":"..."}`; a malformed request answers
`400`, and a route that needs a precondition that is not met (seeking while
nothing plays, for instance) answers `409`. `GET /` describes the API as plain
text.

A request that carries an `Origin` header, which browsers add for web pages,
or whose `Host` is not `127.0.0.1:<port>` or `localhost:<port>`, is refused
with `403`. That keeps a web page open in a browser from driving the phone
through the API, including through DNS rebinding; programs such as `curl` or a
script send neither and are unaffected.

Touch, mouse, keyboard, gamepad, pen, digitizer, and media-key routes all forward through
Live control (see the Live tab in [GUI.md](GUI.md)): call `POST /live/start` once first,
same as turning the Live tab's own toggle on, or they are silently no-ops.
The API's own touch contact is the second-to-last one the connection
declares — one below the Live tab's own (see *Contacts* under *Profiles* in
[GUI.md](GUI.md)) — so a program using the API and a person using the Live tab can
both touch the screen at once without colliding.

`/refresh`, `/connect`, `/hid`, `/battery`, `/record/*`, and `/bridge` are carried out by
the window on its next frame, exactly as its buttons are, with the settings
the window shows (each device's HIDs, Recorder device, bridge port). The request waits for
that frame and answers `503` if the window does not respond within 5 seconds;
a minimized window still answers. Refreshing finishes in the background: poll
`GET /status` until `busy` is `false` before connecting. Connecting also
finishes in the background: poll until `phase` is `connected` (or back to
`idle` if it failed).

| Route | Params | Notes |
|---|---|---|
| `GET /status` | — | `phase`, `connected`, `device_count`, `active` (the connected devices still responding), `devices` (one object per connected device, dropped ones included, in the order `/hid` and `/bridge` number them: `label`, `active`, `bridge` (its bridge port, 0 = off), `reports`, `skipped` (rows it refused as outside its settings), `profiles` (the HIDs it has now), `touch` (`width`, `height`, `contacts`) when it has a touchscreen, and `battery` (the level of each HID that reports one, such as `{"key":80}`)), `space` (the `width` and `height` `/touch` positions are given in, 0 when no device has a touchscreen), `busy` (the startup adb check or a refresh is running), `available` (the device list's labels, in the order `/connect` numbers them), `live_active`, `recording`, and the playback state, `lap` (the lap being played, counted from 1), time, loops, and reports. |
| `POST /refresh` | — | The Devices tab's refresh: stops the adb server (unless recording), then rescans; `busy` is `true` until the new list is in `available`. 409 unless idle and not already busy. |
| `POST /connect` | `devices` (`all`, or 1-based numbers such as `1,3`; default: the current selection) | Connects each device with its own saved HIDs and settings; `devices` also becomes the window's selection. 409 unless idle and not `busy`; 400 for an unknown device number, invalid settings, or when no chosen device has a HID on. |
| `POST /disconnect` | — | 409 if not connected. |
| `POST /play` | `script`, `loop` (laps, the first included; 0 or omitted = repeat until stopped) | `script` is a path, or a bare name looked up in `csv/` (`.csv` added if missing), same as the Player tab's picker. 409 if not connected (or already playing); 400 for a `loop` that is not a non-negative number. |
| `POST /stop` | — | |
| `POST /pause` | `paused` (`true`/`false`, default `true`) | 409 if nothing is playing. |
| `POST /seek` | `lap` (a lap number, counted from 1; default: the lap being played), `time_ms` | 409 if nothing is playing; 400 for a `lap` below 1 or a missing `time_ms`. |
| `POST /live/start` | — | 409 if not connected. |
| `POST /live/stop` | — | |
| `POST /touch` | `x`, `y`, `state` (`true`/`false`, default `true`) | Coordinates in the `space` `/status` reports (the first touchscreen's resolution), not a fraction; other devices get them scaled to their own. |
| `POST /mouse/move` | `dx`, `dy` | Relative, like a physical mouse. |
| `POST /mouse/button` | `button` (1-based), `down` (default `true`) | |
| `POST /mouse/wheel` | `delta` | |
| `POST /key` | `usage` (HID keyboard usage, decimal or `0x..`, or a CSV [key name](CSV_FORMAT.md#key-names)), `down` (default `true`) | |
| `POST /gamepad/button` | `index` (1-based), `down` (default `true`) | |
| `POST /gamepad/axis` | `index` (0-based), `value` | The connection's own logical range (see the Gamepad profile's *Axis bits*). |
| `POST /gamepad/dpad` | `up`, `down`, `left`, `right` (each `true`/`false`) | |
| `POST /pen` | `x`, `y`, `pressure` (default 0), `in_range` (default `true`), `tip` (default `false`) | Pen surface coordinates, in the same `space` as `/touch`. `tip` needs `in_range`. |
| `POST /digitizer` | the same as `/pen` | For the Digitizer profile; the same surface and rules as `/pen`. |
| `POST /media` | `key` (a [media key name](CSV_FORMAT.md#media-key-names) or usage), `down` | Without `down`, a tap: press, then release in the next report. One media key is held at a time. |
| `POST /record/start` | `name` (optional file name; default: the Recorder's name field, else a timestamp) | Records from the Recorder's adb device into `csv/`. 409 if already recording or adb was not found; 400 for an invalid name. A failure after it starts (for example no phone on adb) is written to the activity log and `recording` in `/status` turns `false`. |
| `POST /record/stop` | — | Stops and saves (nothing is saved if nothing was captured). 409 if not recording. |
| `POST /bridge` | `device` (1-based, connected order), `on` (default `true`), `port` (1024-65535; default: the ADB tab's) | Same as the ADB tab's switch, including `adb connect` / the adb server restart. 409 while a script plays, for a dropped device, or a port another phone uses. |
| `POST /hid` | `device` (1-based, connected order), `profile` (`touch`, `mouse`, `key`, `gamepad`, `pen`, `digitizer`, `toggle`), `on` (default `true`) | Adds or removes one HID on one connected device, as its switch on the Devices tab does, with that device's saved settings. The change is made in the background: read `profiles` in `/status`. 409 while a script plays (pause or stop first), for a dropped device, or an unknown device number; 400 for an unknown profile. |
| `POST /battery` | `device` (1-based, connected order), `profile` (as for `/hid`), `on` (default `true`), `level`, `min`, `max` (each 0-65535; the ones left out stay as saved) | Sets the battery one HID of one connected device reports, as its Battery settings on the Devices tab do. A new level alone is sent without registering the HID again. The change is made in the background: read `battery` in `/status`. 409 while a script plays (pause or stop first), for a dropped device, an unknown device number, or a level outside the range; 400 for an unknown profile or a number that is not 0-65535. |
