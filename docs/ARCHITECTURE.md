# Architecture

How aoahid_player is put together: the core library, the two command-line
programs, and the GUI. It records what is not obvious from one file: which
thread may touch what, how time is kept, and what each layer assumes about
the one below. The user-facing behavior (CSV format, control API routes,
options) is in [README.md](../README.md).

Source references are `path#Lnnn` and are valid at v3.0.1.

## Layers

```text
apps/aoahid_player_gui   Dear ImGui front end, optional local HTTP control API
apps/aoa_touch           plays a script from the command line
apps/aoa_record          records `adb shell getevent` into a script
        │
src/ + include/aoahid_player/    aoahid_player_core (static library, namespace aoap)
        │
libaoahid 1.x (HID over AOA 2.0)  +  aoahid_adb_proxy (vendored, the ADB Bridge)
```

The core has no console I/O. It reports through `EventSink`
(`include/aoahid_player/events.hpp#L14`); each front end decides what
to show. `aoa_record` links the core but never calls libaoahid, which is why
`event_script.hpp` does not include `aoahid.h`.

## Core objects

| Object | File | Role |
| --- | --- | --- |
| `Context` | `src/context.cpp#L96` | Owns the libaoahid Context, always in internal-thread mode with logging off. |
| `Session` | `src/session.cpp#L153` | Owns the Context, the discovery snapshot, and the `DeviceGroup`. |
| `build_spec` | `include/aoahid_player/spec_builder.hpp#L534` | Builds the immutable Spec of one profile from a device's `ProfileSetup`. It is built when a Node is opened and released right after; the Node keeps its own reference. |
| `Device` | `src/device.cpp#L211` | One phone: its libaoahid Device, one Node per profile, and an optional ADB Bridge. |
| `DeviceGroup` | `src/device_group.cpp#L104` | Holds each device with its own setup and Nodes, applies one event to every device that has a Node for it, and submits the dirty profiles. |
| `EventScript`, `LapWalk` | `src/event_script.cpp#L533` | The parsed CSV, its rows grouped by the laps they run on, and the walk over one lap's rows. |
| `Player` | `src/player.cpp#L418` | Runs a script on the calling thread against a `DeviceGroup`. |
| `InputState` | `src/input_state.cpp#L110` | What is currently held, so stop, pause, and seek can release or restore it. libaoahid has no release-all call. |
| `Recorder`, `GeteventParser` | `src/recorder.cpp#L138` | Turn `adb shell getevent -lt` output into script rows. |
| `ChildProcess` | `src/process.cpp#L98` | Runs `adb` without a shell and reads its output with a timeout. |

## The one-thread rule

libaoahid requires every call on one Context to be serialized by the caller.
The core does not lock; it is driven from one thread at a time:

- In `aoa_touch`, the main thread connects and then runs the Player.
- In the GUI, the Engine's worker thread makes every libaoahid call
  (`apps/aoahid_player_gui/engine.cpp#L403`): refresh,
  connect, disconnect, playback, live input, and starting or stopping a
  bridge.

What other threads may do:

- Call the Player's controls (`stop`, `pause`, `resume`, `seek`, `set_speed`,
  the offset, `status`). They only write atomics and wake the playback thread.
- Read `DeviceGroup::snapshot()`, which takes the group's own mutex. That
  mutex guards the slot list and the last error text, nothing else.

## Connecting

`src/session.cpp#L153` takes one `ProfileSetup` per selected device, validates
them, and for each device opens it, opens a Node for every profile its setup
has on, and then, when asked, starts the ADB Bridge. The Channel is opened
after the Nodes.

- A profile that cannot be registered is left off with a warning. A device
  that cannot be opened, or on which no wanted profile could be registered,
  is skipped; the connection succeeds if at least one device is ready. A
  device whose setup has no profile is connected and receives nothing.
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

## Changing a connected device

`src/device_group.cpp#L343` (`DeviceGroup::reconfigure`) brings one device to a
new `ProfileSetup`. It flushes, closes the Nodes that are no longer wanted or
whose settings changed (`aoahid_node_close` sends the Node's neutral report
and unregisters it), opens the missing ones, and stores the setup the device
now has.

- A Node that cannot be closed is still registered in an unknown state, so
  the device is dropped. A Node that cannot be opened stays off; the device
  is kept.
- `Session::reconfigure` waits the same 100 ms after a Node was opened.
- It must not run under a playing script. The GUI's Engine serves these
  requests on its worker thread while stopped, in Live control, or from the
  Player's live pump while the Player is holding a pause, and refuses them
  otherwise (`apps/aoahid_player_gui/engine.cpp#L798`). Whatever Live control
  holds is released first, so its record of what is pressed stays true.
- libaoahid numbers HID IDs per Context and never reuses one. The Context
  lives as long as the `Session`, so about 65000 Nodes can be opened in one
  run of the program; after that a Node cannot be added.

## Applying and sending

`src/device_group.cpp#L187` changes the Node
state on every active device that has a Node for the row's profile, with
touch and pen positions scaled from the row's `Space` onto that device's own
surface. It sends nothing.
`src/device_group.cpp#L273` then submits every dirty
profile on every device, and only after that waits for each completion
(`aoahid_node_submit_blocking`, 500 ms). The wait is as long as the slowest
device, not the sum.

What `apply` does with a libaoahid result:

| Result | Meaning here | Action |
| --- | --- | --- |
| `AOAHID_ERR_UNSUPPORTED` | No active device has the profile. | The caller skips the row and warns once per profile. |
| `AOAHID_ERR_BUSY`, nothing applied yet | The opposite edge of the same control is not reported yet. | The caller flushes and tries once more. |
| `AOAHID_ERR_BUSY`, already applied elsewhere | One device is behind. | The event is dropped for that device only. |
| `AOAHID_ERR_PARAM`, `AOAHID_ERR_OVERFLOW` | The row does not fit what that device's Spec declares (a value out of range, more contacts than slots). | That device skips the row and counts it (`DeviceStatus::skipped`); it is not dropped. `apply` returns `AOAHID_ERR_PARAM` only when every device that has the profile refused. |
| Anything else | The device failed. | That device is closed and marked inactive (`src/device_group.cpp#L153`). |

The group's profile mask is the union of the active devices' profiles; it is
recomputed when a device is added, changed, or dropped. A dropped device is
closed on the thread that noticed, which can hold up the others for the close
budget.

## Scripts and time

- A row's `wait_ms` is the delay after it. It is parsed once, into
  nanoseconds (`src/event_script.cpp#L90`). The accepted
  range is 0 to 1e9 ms.
- `src/event_script.cpp#L807` (`LapWalk`) steps through the rows that run on
  one lap and gives each a start time measured from the start of that lap. A
  lap holds the rows outside any block plus those of the blocks whose
  selector (first, last, every, negated for `@else`) includes its number; a
  `keep-time` block that does not run still adds its duration.
- Every deadline is absolute:
  `anchor_real + (script_time - anchor_script) / speed + offset`
  (`src/player.cpp#L166`). A slow transfer delays
  one row, never the ones after it. A row that is already late is sent at once.
- A speed change, a seek, and a lap end re-anchor. A lap end moves the real
  anchor by exactly the lap's length, so laps do not drift.
- Rows with wait 0 share a report with the next row. A row whose control is
  already staged in the batch flushes first
  (`src/event_script.cpp#L330`): touch per finger, buttons
  and keys per number, axes per index; the dpad, the pen, and the media key
  are one control each; mouse motion accumulates and never conflicts.
- Coordinates are scaled as a row is applied, for each device
  (`include/aoahid_player/event_script.hpp#L165`, `scale_coordinate`), from
  the space the file names onto that device's own surface: two integer
  multiply-and-divides per touch or pen row, and none when the sizes match.
  Without a named space they are sent as written.

`src/timing.cpp#L164` sleeps until 100 µs before the
deadline and then spins. The sleep is a futex wait on the Player's
`WakeSignal` (an event on Windows), so a control or live input ends it
immediately. The clock is `CLOCK_MONOTONIC` or QueryPerformanceCounter.

## Player controls

Controls set a bit in `requests_` and wake the playback thread
(`src/player.cpp#L45`). The thread acts on them in
`src/player.cpp#L299`.

- Pause flushes, releases everything held, and waits. Resume and seek rebuild
  the state the script would have at the target
  (`src/input_state.cpp#L172`) and send the difference: releases
  in one report, presses in the next (`src/player.cpp#L225`). The state at
  the start of a lap comes from the latest earlier lap each block ran on, so
  the cost does not grow with the lap number.
- `status()` is a seqlock read of values the playback thread publishes; the
  position while playing is interpolated from the anchor.
- A control posted while no `run()` is active stays pending and applies when
  the next `run()` starts. That is what lets `aoa_touch` honor Ctrl+C pressed
  just before playback, and it is why an owner that runs several scripts must
  call `src/player.cpp#L83` between them.
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
| UI | GLFW, Dear ImGui, `apps/aoahid_player_gui/app.cpp#L1210` | All `App` state. |
| Engine worker | `apps/aoahid_player_gui/engine.cpp#L403` | The `Session`, the `Player`, and every libaoahid call. |
| HTTP pool | cpp-httplib, 1 to 8 threads (`apps/aoahid_player_gui/control_api.cpp#L348`) | Nothing; see below. |
| Recorder, paste, adb tasks | Short-lived helpers | Their own work only. |

The Engine is a command queue. The UI and HTTP threads push commands; the
worker executes one at a time. `phase` is set optimistically when a command
is queued, so the UI reflects a click at once.
`apps/aoahid_player_gui/engine.cpp#L83` checks the phase
and queues under one lock, so two callers cannot both start a playback.

Live control does not use the queue for input. Input goes into an inbox
(`apps/aoahid_player_gui/engine.cpp#L216`) where moves
are folded together and edges keep their order, and
`apps/aoahid_player_gui/engine.cpp#L507` drains it into
one report. While a script plays, the Player calls the same pump between
rows, so live input and playback share one thread and one `DeviceGroup`.

When a playback command ends,
`apps/aoahid_player_gui/engine.cpp#L450` drops any
Player control still pending, under the lock that `stop()` posts under.

The render loop is event-driven (`apps/aoahid_player_gui/main.cpp#L391`):
it draws when input arrives or a worker wakes it, and sleeps otherwise.
Settings are compared with the saved copy on frames with no active widget and
written to a temporary file that replaces the `.ini`; a window that is being
moved or resized is saved when it comes to rest
(`apps/aoahid_player_gui/app.cpp#L237`).

## Control API

`apps/aoahid_player_gui/control_api.cpp#L339`
binds `127.0.0.1` only. It is off by default and has no authentication.

- A request with an `Origin` header, or whose `Host` is not
  `127.0.0.1:<port>` or `localhost:<port>`, gets 403 before routing. This is
  what keeps a web page, including one using DNS rebinding, from driving it.
- The listening socket is not shareable: cpp-httplib's default
  (`SO_REUSEPORT`) is replaced, so a second program cannot bind the same port
  and receive part of the requests.
- Routes that only need the Engine call it from the HTTP thread: playback
  controls and all live input.
- Routes that need `App` state (`/refresh`, `/connect`, `/hid`, `/record/*`,
  `/bridge`) post a request the UI thread runs on its next frame
  (`apps/aoahid_player_gui/app.cpp#L360`) and wait
  up to 5 seconds for it.
- A boolean parameter that is present must be a recognised spelling, or the
  route answers 400 (`apps/aoahid_player_gui/control_api.cpp#L132`).
- The API's touch contact is the second-to-last declared one, so it does not
  collide with the Live tab's, which is the last. With several touchscreens
  both are counted from the one that declares the fewest contacts, and
  positions are in the first touchscreen's space
  (`Engine::live_setup()`).

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
  (`apps/aoahid_player_gui/widgets.cpp#L764`).

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

`tests/` uses doctest and covers the script parser and laps, `batch_key`,
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
