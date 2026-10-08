# Architecture

How aoahid_player is put together: the core library, the two command-line
programs, and the GUI. It records what is not obvious from one file: which
thread may touch what, how time is kept, and what each layer assumes about
the one below. The user-facing behavior (CSV format, control API routes,
options) is in [README.md](../README.md).

Source references are `path#Lnnn` and are valid at v1.1.3.

## Layers

```text
apps/aoahid_player_gui   Dear ImGui front end, optional local HTTP control API
apps/aoa_touch           plays a script from the command line
apps/aoa_record          records `adb shell getevent` into a script
        │
src/ + include/aoahid_player/    aoahid_player_core (static library, namespace aoap)
        │
libaoahid 4.x (HID over AOA 2.0)  +  aoahid_adb_proxy (vendored, the ADB Bridge)
```

The core has no console I/O. It reports through `EventSink`
(`include/aoahid_player/events.hpp#L14`); each front end decides what
to show. `aoa_record` links the core but never calls libaoahid, which is why
`event_script.hpp` does not include `aoahid.h`.

## Core objects

| Object | File | Role |
| --- | --- | --- |
| `Context` | `src/context.cpp#L96` | Owns the libaoahid Context, always in internal-thread mode with logging off. |
| `Session` | `src/session.cpp#L148` | Owns the Context, the discovery snapshot, the Specs, and the `DeviceGroup`. |
| `SpecSet` | `include/aoahid_player/spec_builder.hpp#L240` | One immutable Spec per enabled profile, built once per connection and shared by every device. |
| `Device` | `src/device.cpp#L207` | One phone: its libaoahid Device, one Node per profile, and an optional ADB Bridge. |
| `DeviceGroup` | `src/device_group.cpp#L139` | Applies one event to every device and submits the dirty profiles. |
| `EventScript`, `Timeline` | `src/event_script.cpp#L429` | The parsed CSV and the start time of every row per lap. |
| `Player` | `src/player.cpp#L420` | Runs a script on the calling thread against a `DeviceGroup`. |
| `InputState` | `src/input_state.cpp#L111` | What is currently held, so stop, pause, and seek can release or restore it. libaoahid has no release-all call. |
| `Recorder`, `GeteventParser` | `src/recorder.cpp#L138` | Turn `adb shell getevent -lt` output into script rows. |
| `ChildProcess` | `src/process.cpp#L98` | Runs `adb` without a shell and reads its output with a timeout. |

## The one-thread rule

libaoahid requires every call on one Context to be serialized by the caller.
The core does not lock; it is driven from one thread at a time:

- In `aoa_touch`, the main thread connects and then runs the Player.
- In the GUI, the Engine's worker thread makes every libaoahid call
  (`apps/aoahid_player_gui/engine.cpp#L370`): refresh,
  connect, disconnect, playback, live input, and starting or stopping a
  bridge.

What other threads may do:

- Call the Player's controls (`stop`, `pause`, `resume`, `seek`, `set_speed`,
  the offset, `status`). They only write atomics and wake the playback thread.
- Read `DeviceGroup::snapshot()`, which takes the group's own mutex. That
  mutex guards the slot list and the last error text, nothing else.

## Connecting

`src/session.cpp#L148` validates the setup, builds the
Specs, and for each selected device opens it, opens one Node per Spec, and
then, when asked, starts the ADB Bridge. The Channel is opened after the Nodes.

- A device that fails any step is skipped with a warning; the connection
  succeeds if at least one device is ready.
- The bridge port is `first_port + index` (6555 by default). A bridge that
  does not start is a warning, not a failure.
- Android registers a HID device after `aoahid_node_open` has returned, and a
  report sent before that is refused with a STALL. `connect` therefore waits
  100 ms before it returns
  (`src/session.cpp#L18`). libaoahid's
  `docs/API.md` asks for this gap.
- The device options (`src/device.cpp#L26`)
  are this program's policy: 500 ms control and send timeouts, an 8-slot
  transfer pool with one slot reserved per Node, `validate_reports` off, and
  the five Linux HID parser limits from libaoahid's `docs/LIMITS.md`.
- The device is opened with `AOAHID_INTERFACE_CLAIM_NONE`. HID goes over the
  control endpoint, so no interface is claimed until the bridge claims ADB's.

Only devices that answered AOA request 51 with version 2 or more are listed.

## Applying and sending

`src/device_group.cpp#L139` changes the Node
state on every active device. It sends nothing.
`src/device_group.cpp#L233` then submits every dirty
profile on every device, and only after that waits for each completion
(`aoahid_node_submit_blocking`, 500 ms). The wait is as long as the slowest
device, not the sum.

What `apply` does with a libaoahid result:

| Result | Meaning here | Action |
| --- | --- | --- |
| `AOAHID_ERR_UNSUPPORTED` | No connected device has the profile. | The caller skips the row and warns once per profile. |
| `AOAHID_ERR_BUSY`, nothing applied yet | The opposite edge of the same control is not reported yet. | The caller flushes and tries once more. |
| `AOAHID_ERR_BUSY`, already applied elsewhere | One device is behind. | The event is dropped for that device only. |
| `AOAHID_ERR_PARAM`, `AOAHID_ERR_OVERFLOW` | The row does not fit what the Spec declares (a value out of range, more contacts than slots). | The row is rejected for every device; no device is dropped. |
| Anything else | The device failed. | That device is closed and marked inactive (`src/device_group.cpp#L110`). |

The profile mask is the intersection of all devices' profiles and is not
recomputed when a device drops. A dropped device is closed on the thread that
noticed, which can hold up the others for the close budget.

## Scripts and time

- A row's `wait_ms` is the delay after it. It is parsed once, into
  nanoseconds (`src/event_script.cpp#L90`). The accepted
  range is 0 to 1e9 ms.
- `src/event_script.cpp#L622` gives every row a start
  time measured from the start of its lap. The first lap holds every row. The
  repeat lap holds only the rows that are not once-only; a `keep-time` row
  still adds its wait there.
- Every deadline is absolute:
  `anchor_real + (script_time - anchor_script) / speed + offset`
  (`src/player.cpp#L167`). A slow transfer delays
  one row, never the ones after it. A row that is already late is sent at once.
- A speed change, a seek, and a lap end re-anchor. A lap end moves the real
  anchor by exactly the lap's length, so laps do not drift.
- Rows with wait 0 share a report with the next row. A row whose control is
  already staged in the batch flushes first
  (`src/event_script.cpp#L316`): touch per finger, buttons
  and keys per number, axes per index; the dpad, the pen, and the media key
  are one control each; mouse motion accumulates and never conflicts.
- Coordinates are scaled once, before playback
  (`src/event_script.cpp#L395`), from the space the file names
  onto the connected surface. Without a named space they are used as written.

`src/timing.cpp#L164` sleeps until 100 µs before the
deadline and then spins. The sleep is a futex wait on the Player's
`WakeSignal` (an event on Windows), so a control or live input ends it
immediately. The clock is `CLOCK_MONOTONIC` or QueryPerformanceCounter.

## Player controls

Controls set a bit in `requests_` and wake the playback thread
(`src/player.cpp#L54`). The thread acts on them in
`src/player.cpp#L303`.

- Pause flushes, releases everything held, and waits. Resume and seek rebuild
  the state the script would have at the target
  (`src/input_state.cpp#L163`) and send the difference: releases
  in one report, presses in the next (`src/player.cpp#L224`).
- `status()` is a seqlock read of values the playback thread publishes; the
  position while playing is interpolated from the anchor.
- A control posted while no `run()` is active stays pending and applies when
  the next `run()` starts. That is what lets `aoa_touch` honor Ctrl+C pressed
  just before playback, and it is why an owner that runs several scripts must
  call `src/player.cpp#L84` between them.
- The wake epoch is read before the live pump runs. Input queued while the
  pump runs then ends the next wait instead of being slept through.

## Recording

`src/recorder.cpp#L138` reads the panel's coordinate range
with `getevent -lp`, then streams `getevent -lt`.

- Only multi-touch protocol type B is understood: `ABS_MT_SLOT` selects a
  slot, `ABS_MT_TRACKING_ID` places a contact or (with -1) lifts it, and
  `SYN_REPORT` closes a frame
  (`src/getevent_parser.cpp#L198`). 16 slots are
  tracked; events for a higher slot are dropped.
- A frame's wait is known only when the next frame arrives, so rows are
  emitted one frame late and the last frame gets wait 0.
- The rows go to `<name>.part` and replace `<name>` only when the recording
  stops with at least one row (`src/recorder.cpp#L127`).
  A recording that cannot start, or captures nothing, leaves an existing
  script of that name untouched.
- `adb connect` exits 0 even when it fails; success is read from its output.

## adb

Every adb call goes through `src/adb.cpp#L75`
and `run_command`, as an argument vector. No shell is involved on the host.
`adb shell` arguments are interpreted by the phone's shell.

The adb server and this program compete for the phone's ADB interface. The
GUI stops the server when it refreshes the device list and after it
disconnects, and the bridge start retries once after stopping it. The reasons
are in the vendored proxy's upstream `docs/USAGE.md`.

## GUI threads

| Thread | Runs | Owns |
| --- | --- | --- |
| UI | GLFW, Dear ImGui, `apps/aoahid_player_gui/app.cpp#L1071` | All `App` state. |
| Engine worker | `apps/aoahid_player_gui/engine.cpp#L370` | The `Session`, the `Player`, and every libaoahid call. |
| HTTP pool | cpp-httplib, 1 to 8 threads (`apps/aoahid_player_gui/control_api.cpp#L294`) | Nothing; see below. |
| Recorder, paste, adb tasks | Short-lived helpers | Their own work only. |

The Engine is a command queue. The UI and HTTP threads push commands; the
worker executes one at a time. `phase` is set optimistically when a command
is queued, so the UI reflects a click at once.
`apps/aoahid_player_gui/engine.cpp#L77` checks the phase
and queues under one lock, so two callers cannot both start a playback.

Live control does not use the queue for input. Input goes into an inbox
(`apps/aoahid_player_gui/engine.cpp#L210`) where moves
are folded together and edges keep their order, and
`apps/aoahid_player_gui/engine.cpp#L461` drains it into
one report. While a script plays, the Player calls the same pump between
rows, so live input and playback share one thread and one `DeviceGroup`.

When a playback command ends,
`apps/aoahid_player_gui/engine.cpp#L409` drops any
Player control still pending, under the lock that `stop()` posts under.

The render loop is event-driven (`apps/aoahid_player_gui/main.cpp#L391`):
it draws when input arrives or a worker wakes it, and sleeps otherwise.
Settings are compared with the saved copy on frames with no active widget and
written to a temporary file that replaces the `.ini`; a window that is being
moved or resized is saved when it comes to rest
(`apps/aoahid_player_gui/app.cpp#L305`).

## Control API

`apps/aoahid_player_gui/control_api.cpp#L285`
binds `127.0.0.1` only. It is off by default and has no authentication.

- A request with an `Origin` header, or whose `Host` is not
  `127.0.0.1:<port>` or `localhost:<port>`, gets 403 before routing. This is
  what keeps a web page, including one using DNS rebinding, from driving it.
- The listening socket is not shareable: cpp-httplib's default
  (`SO_REUSEPORT`) is replaced, so a second program cannot bind the same port
  and receive part of the requests.
- Routes that only need the Engine call it from the HTTP thread: playback
  controls and all live input.
- Routes that need `App` state (`/refresh`, `/connect`, `/record/*`,
  `/bridge`) post a request the UI thread runs on its next frame
  (`apps/aoahid_player_gui/app.cpp#L345`) and wait
  up to 5 seconds for it.
- A boolean parameter that is present must be a recognised spelling, or the
  route answers 400 (`apps/aoahid_player_gui/control_api.cpp#L132`).
- The API's touch contact is the second-to-last declared one, so it does not
  collide with the Live tab's, which is the last.

## Files next to the executable

| Path | Content |
| --- | --- |
| `aoahid_player_gui.ini` | Settings, `key = value`. Unknown keys are ignored. |
| `csv/` | Scripts and recordings. |
| `playlists/<name>.playlist` | A list of scripts with loop counts. |
| `themes/<name>.theme` | A saved look; only the appearance keys are applied. |

## Look and feel

The GUI doubles as a showcase for the libraries, so appearance is a goal of
its own here. The rules the code follows:

- Colors are tokens in `theme.cpp`, derived from the mode (dark or light),
  the accent, and whether the glass look is on
  (`apps/aoahid_player_gui/theme.cpp#L240`). Widgets use tokens,
  not literal colors.
- An accent chosen by the user is adjusted until text on it reaches a 4.5:1
  contrast ratio (`apps/aoahid_player_gui/theme.cpp#L132`).
- Sizes go through `px()`, which applies the display scale and the UI scale.
- The background picture is decoded once, capped at 2560 px on its long side,
  and blurred once at 768 px; cards sample that texture instead of blurring
  per frame.
- Text that states a size must match what is drawn. The header's status row
  is right-aligned from widths computed the same way its items are laid out
  (`apps/aoahid_player_gui/widgets.cpp#L748`).

## Third-party code

| Library | Version | How |
| --- | --- | --- |
| Dear ImGui | 1.92.9 | Fetched at configure time, hash-pinned. |
| GLFW | 3.5.1 | Fetched, hash-pinned, patched by `cmake/PatchGlfwXim.cmake`. |
| cpp-httplib | 0.57.1 | Fetched, hash-pinned, header-only. |
| nativefiledialog-extended | 1.4.1 | Fetched, hash-pinned; the portal backend on Linux. |
| stb_image | vendored | `third_party/stb/`. |
| doctest | vendored | `tests/third_party/`. |
| aoahid_adb_proxy | vendored | `third_party/aoahid_adb_proxy/`, compiled into the core. |

The versions are set in `apps/aoahid_player_gui/CMakeLists.txt#L13`
and the lines after it.

## Tests

`tests/` uses doctest and covers the script parser and timeline, `batch_key`,
`InputState`, the getevent parser, the recorder's formatting and file
hand-over, the Spec helpers, and the adb string helpers. `Player`,
`DeviceGroup`, `Session`, and the GUI have no automated tests; they need a
device or a stand-in for libaoahid.

## Sources

- Linux multi-touch protocol, type B ("A non-negative tracking id is
  interpreted as a contact, and the value -1 denotes an unused slot"):
  <https://www.kernel.org/doc/html/latest/input/multi-touch-protocol.html>.
- `getevent` and its `-l`, `-t`, and `-p` options:
  <https://source.android.com/docs/core/interaction/input/getevent>.

Both retrieved 2026-10-04.
