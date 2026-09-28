# aoahid-player

[![ci](https://github.com/nemarpuc/aoahid_player/actions/workflows/ci.yml/badge.svg)](https://github.com/nemarpuc/aoahid_player/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Plays scripted touch, mouse, keyboard, gamepad, and pen input to Android
devices over USB with AOA 2.0 HID, and records real touches and key presses
into the same script format with `adb`. Linux and Windows, x86_64 and ARM64,
C++20.

There are three programs, all built on one shared engine:

- `aoahid_player_gui` — a desktop app for connecting, playing, seeking, and
  recording.
- `aoa_touch` — the command-line player.
- `aoa_record` — the command-line recorder.

All HID, AOA, and USB work is done by
[libaoahid](https://github.com/nemarpuc/libaoahid); see that repository for
the protocol, descriptor, and transport details.

## Download

Each [release](https://github.com/nemarpuc/aoahid_player/releases) has one
archive per platform and CPU (`linux-x86_64`, `linux-aarch64`,
`windows-x86_64`, `windows-arm64`) with everything in a single folder:

```
aoahid-player-<version>-<platform>-<cpu>/
  aoahid_player_gui[.exe]   aoa_touch[.exe]   aoa_record[.exe]
  libaoahid + libusb runtime (.so / .dll)
  MSVC runtime DLLs (Windows only)
  csv/            scripts; recordings are saved here
  third-party/    licences of everything bundled
  README.md  LICENSE
```

Keep the folder together: the programs find the libraries and `csv/` next to
themselves. On Linux, install the udev rule first (see
[Linux permissions](#linux-permissions-udev)). Recording needs `adb`
(Android SDK Platform-Tools) on PATH; playback does not. The Windows ARM64
archive is cross-compiled, so CI checks its contents but does not run it.

## The GUI

Start `aoahid_player_gui`. The profile settings are saved to
`aoahid_player_gui.ini` next to the program whenever you finish changing them,
and restored at the next start; delete the file to go back to the defaults.
If the folder is read-only, changes last until the app closes.

On startup, before the first device scan, the app starts the adb server,
reads the touchscreen resolution with `adb shell wm size`, and stops the
server again (it can otherwise hold the phone's USB interface and block the
accessory handshake) — all once, so Connect itself never has to wait on adb.
An *adb* status pill next to the connection status (top right) shows whether
the server is running, stopped, not found, or not yet known, and each change
is logged.

**Left side — connection**

1. *Devices* lists AOA-capable phones; the refresh button stops the adb
   server (in case something else started it since) and searches again,
   without touching the screen size or connecting on its own — press Connect
   once the phone reappears. Tick one or more (a single phone is ticked for
   you).
2. *Profiles* chooses which HID devices to present: touchscreen, mouse,
   keyboard, gamepad, pen, toggle. Each one's settings open when it is
   switched on.
   The touchscreen resolution comes from the startup adb read described above
   (an override size wins, because that is what touches map to); type into
   the width/height fields to set it by hand instead. *Contacts* (16 by
   default, the maximum) is how many simultaneous fingers the touchscreen
   declares; the Live tab always uses the last one for its own pointer (see
   *Live tab*, below), so a script gets one fewer than this to itself.
3. *Connect* starts the AOA handshake on the ticked devices right away —
   it does not touch adb or rescan, so it stays fast how ever many devices
   are ticked. *Accessory Mode* switches the ticked devices into AOA
   accessory mode without taking the connection; to leave this mode
   afterwards, physically unplug and replug the USB cable.

While connected, the profile choices are locked; disconnect to change them.
Each connected phone shows a status dot and its report count (and its ADB
Bridge port, see *ADB tab*), and a phone that stops responding is dropped
without stopping playback for the others.

**Player tab**

- The *Script* box shows the loaded script. Click it (or press **Ctrl+F**) to
  open the script list: type to search (words match anywhere in the name,
  case-insensitively), move with **Up/Down**, open with **Enter** or a click,
  close with **Esc**. Each row shows the file size and when it was last
  changed. The trash button deletes a file after an inline confirmation.
  Files outside `csv/` open by path from the bottom of the list, or by
  dropping them on the window. The list rescans whenever the window gets
  focus.
- Play/pause (also **Space**), stop (**Esc** or **Ctrl+S**), and back-to-start
  (**Home** or **Backspace**) buttons. Each key can be remapped with the
  *Set...* buttons under the transport; a remapped action answers to that one
  key only, and *Reset* restores the defaults. One bar shows
  the whole cycle: the first lap (rows that run only once in purple, repeated
  rows in green, in the order the file writes them), a gap, then the repeat
  lap (green). Click or drag it to seek across both, while playing, paused, or
  stopped. A script with no once-only rows shows a single lap. The label reads
  *Loop k of N*, or *Once* for a script with nothing to repeat.
- A script with problems shows every one of them (six lines here, all of them
  in `aoa_touch`). A script whose repeat lap ends in a different input state
  than it began in gets a warning, in the card and in the Activity log; see
  [Checks](#checks).
- *Speed* is typed in directly (0.01×–1000×, with 0.5× / 1× / 2× / 4×
  buttons) and *Loops* counts laps, the first included (0 repeats until
  stopped); both apply immediately, even in the middle of a wait. *Offset* shifts every later event by ±1 or ±10 ms;
  it starts at zero on every run and can only be changed while playing.
- Pause and stop release everything the script was holding. A seek rebuilds
  exactly what would be held at the new position (fingers down, keys,
  buttons, axes), so playback continues as if it had never jumped.

**Live tab**

A black preview with the phone's shape, for using the phone from the computer
and watching what the scripts do. Live control starts as soon as the tab is
open while a phone is connected, and the switches under the preview choose
what it forwards: *Touch* (drag inside the preview), *Mouse* (motion, buttons,
and the wheel), *Keyboard*, *Gamepad* (any controller GLFW recognises), and
*Toggle* (media and system keys). Only profiles the connection actually has
can be turned on, and touch and mouse share the pointer, so only one of them
is on at a time. The panel beside the preview lists what is held and what was
sent, including an *Active touches* line per finger currently down — Live's
own included, called out from the rest — since a script can be playing at the
same time (see below); the same list also appears on the Player tab. The Live
finger itself appears on the preview with its coordinates, held keys along
its bottom. *Rotate* turns the preview a quarter
turn at a time for landscape use, and the two numbers beside it set its shape
as width:height; both default to the connected touchscreen, and *Reset*
returns to it. Neither changes where a touch lands: the pointer's position
inside the preview is taken as a fraction, turned back into the phone's own
orientation, and only then scaled to the connected resolution. The expand
button switches the preview to a real, OS-level full screen window (not just
this window's own layout filling its still-windowed frame) with just the
preview and the input switches (including toggle buttons), reached by right-clicking the preview —
the only way in, so nothing pops up on its own; Mouse mode's own right
button is not taken until the pointer is actually captured, so the menu
still works right up to that point. *Exit full screen* is in that menu.
There is no keyboard shortcut for full screen any more, so it never
conflicts with a key a script or the target app needs. The preview shows why
Live control is unavailable right now (no device connected) under itself, in
both the normal and the full-screen view, instead of just a disabled toggle.
Live control now keeps running while a script plays, instead of turning off
when playback starts: its touch contact is always the last one the connection
declares (see *Contacts* under *Profiles*, above), so it never collides with
whichever ones the script itself is using.

Mouse mode sends relative motion, so clicking inside the preview captures
the pointer: the cursor disappears and stops being bounded by the screen
edge, the same way a first-person game grabs the mouse, which keeps motion
flowing however far and fast it moves. It also captures the wheel, so
scrolling never leaks into the surrounding window while the pointer is
grabbed. While captured, the pointer belongs to the preview alone: it never
drifts onto — and click — some other button, tab, or setting in this window,
however far or long it moves; the window's own controls simply see no mouse
until the capture is released. **Esc** releases the capture by default
without turning off Live control or the Mouse toggle, and is still forwarded
to the phone like any other key; clicking the preview again re-captures it.
*Mouse release key*, below the preview, lets a different key take over
instead — useful when Esc itself needs to reach the phone (a game that quits
on Esc, for instance). Once changed, only that exact key releases the
capture; every other key, Esc included, goes straight to the phone like
normal. While captured, the release key is also spelled out directly on the
preview itself ("Press Esc to release the pointer", or whichever key was
chosen), including in full screen, so it stays visible without looking away
from the phone.

Keyboard forwarding recognises the extra keys a JIS (Japanese) keyboard has
that a US layout does not — Henkan, Muhenkan, Kana, Zenkaku/Hankaku, and Ro —
on Linux, by their physical scancode rather than by name: GLFW has no
`GLFW_KEY_*` constant for any of them on any platform, so without this they
would never reach the phone at all. This was a gap in this app talking to
GLFW, not a limitation of the phone or Android — once a usage reaches the
connection it is forwarded like any other key. The connection's *Usages*
range (in the Keyboard profile settings) has to include 87-94 for these to
actually reach the phone; raise its upper end from the default 65 if you
need them. ISO keyboards (the extra key beside left Shift, common outside
the US) are already covered by the default range on every platform. Windows
uses a different scancode numbering than Linux, so this recognition is
Linux-only for now; on Windows these five keys still do not reach the phone.

*Reference image* loads a picture (a screenshot works well) over the
preview, by path or by dropping the file on the preview while this tab is
open. While unlocked, drag inside it to move, its corner handle to resize,
and its top handle to rotate; *Lock image* freezes it in place and lets
touches and clicks pass straight through to the phone. Its path, position,
size, rotation, and opacity are saved between runs, and the image is loaded
again at the next start while the file is still there.

**Playlist tab**

Plays several scripts in order, each for its own number of loops, with an
optional limit in minutes on the whole run. Playlists are saved as
`playlists/<name>.playlist` next to the program and reopened from *Open*.

**Recorder tab**

Choose the phone (automatic works when adb sees only one; turning on a
phone's bridge in the *ADB tab* chooses its `127.0.0.1:<port>`) and optionally limit
it to one `/dev/input/eventN`. Type the *File name* before you start (`.csv` is
added; blank gives `record-<date>-<time>`). Names with characters Windows or
Linux cannot store are refused, and a name that already exists turns the
button into *Replace and record*. *Stop and save* writes the file into `csv/`,
and *Open in player* loads it.

*Coordinates* chooses how touch positions are written: **Raw** (the panel's own
numbers) or **Normalized** (fractions from 0 to 1). Each row shows what a tap
in the middle of the screen looks like. Where a touch lands is the same in
both. The choice is remembered and is fixed while recording; see
[`--coords`](#aoa_record--usage).

**ADB tab**

While this app holds a phone over USB, adb cannot open it on its own — on
Windows never, because a USB device can be opened by only one program. The
*ADB* tab keeps adb working anyway: each connected phone has its own *Bridge*
switch and *Port*. Turning a bridge on serves that phone's adb interface on
`127.0.0.1:<port>` with the built-in
[aoahid_adb_proxy](https://github.com/nemarpuc/aoahid_adb_proxy), then runs
`adb connect 127.0.0.1:<port>` for you, so the phone shows up in
`adb devices`, `adb -s 127.0.0.1:<port> shell` works, and the Recorder tab
selects it. *Copy command* copies that `adb -s ... shell` line; it is only a
convenience.

Nothing starts on its own at Connect. Ports start at 6555 (the next phone
gets the next free one) and are remembered per phone; change one while its
bridge is off, and keep it outside 5555-5585, which adb scans for emulators.
The phone needs USB debugging on; the first time, it may ask to allow this
computer, the same prompt as over USB.

Turning a bridge on works in this order: the phone is already connected and
its HID devices registered; the bridge claims the phone's adb interface. If
an adb server holds that interface, the server is stopped and the bridge
tried once more (the other bridges are then reconnected to adb); a driver
problem (`libusb status -12`) skips this, since stopping adb cannot fix it.
Finally `adb connect` runs off the UI thread. Turning it off, or disconnecting,
closes the bridge before the phone is closed and runs `adb disconnect`.
Bridges cannot be switched while a script plays, since stopping one can take
up to a second.

The bridge has been verified end to end on real hardware with a Samsung
tablet and a HyperOS phone, on both Linux and Windows. On Windows the HyperOS
phone worked as plugged in, while the Samsung tablet first needed its driver
switched to WinUSB. If a bridge does not start on Windows, see
[Troubleshooting](#troubleshooting).

On a Wayland session the GUI opens through XWayland, because GLFW's native
Wayland backend has no input method support; that is what lets the script
search (and every other text field) take Japanese and other IME input via
fcitx5 or ibus (`XMODIFIERS` is set to `@im=fcitx` when unset).

The window only redraws when something changes, so an idle window uses no
CPU. It follows the monitor's scale factor on Windows and X11 and the
compositor's scale on Wayland.

## Control API

A local HTTP server another program can use to drive the connected phone —
the same things Live control and the Player tab do, callable over plain
HTTP. Off by default; turn it on from the sidebar's *Control API* card
(*Enabled*, and the *Port* it listens on, 47821 by default). It binds
`127.0.0.1` only, never a public interface, but has no further
authentication: any program running on this machine can reach it once it is
on, so turning it on is a deliberate choice, same as opening a debug port.

Every route takes plain query parameters — `curl -X POST
"http://127.0.0.1:47821/touch?x=500&y=900&state=true"` — with no request body
to build. `GET /` and `GET /status` are the only GET routes; every other route
is POST. A route answers `200` with a small JSON object, at least
`{"ok":true}` or `{"ok":false,"error":"..."}`; a malformed request answers
`400`, and a route that needs a precondition that is not met (seeking while
nothing plays, for instance) answers `409`. `GET /` describes the API as plain
text.

A request that carries an `Origin` header, which browsers add for web pages,
or whose `Host` is not `127.0.0.1:<port>` or `localhost:<port>`, is refused
with `403`. That keeps a web page open in a browser from driving the phone
through the API, including through DNS rebinding; programs such as `curl` or a
script send neither and are unaffected.

Touch, mouse, keyboard, and gamepad routes all forward through
Live control (see the Live tab, above): call `POST /live/start` once first,
same as turning the Live tab's own toggle on, or they are silently no-ops.
The API's own touch contact is the second-to-last one the connection
declares — one below the Live tab's own (see *Contacts* under *Profiles*,
above) — so a program using the API and a person using the Live tab can
both touch the screen at once without colliding.

| Route | Params | Notes |
|---|---|---|
| `GET /status` | — | Phase, connected device count, active profiles, `live_active`, and the playback state/position/loops/reports. |
| `POST /play` | `script`, `loop` (0 = repeat until stopped) | `script` is a path, or a bare name looked up in `csv/` (`.csv` added if missing), same as the Player tab's picker. 409 if not connected (or already playing). |
| `POST /stop` | — | |
| `POST /pause` | `paused` (`true`/`false`, default `true`) | 409 if nothing is playing. |
| `POST /seek` | `lap` (`first`/`repeat`), `time_ms` | 409 if nothing is playing. |
| `POST /live/start` | — | 409 if not connected. |
| `POST /live/stop` | — | |
| `POST /touch` | `x`, `y`, `state` (`true`/`false`, default `true`) | Device coordinates, not a fraction. |
| `POST /mouse/move` | `dx`, `dy` | Relative, like a physical mouse. |
| `POST /mouse/button` | `button` (1-based), `down` (default `true`) | |
| `POST /mouse/wheel` | `delta` | |
| `POST /key` | `usage` (HID keyboard usage, decimal or `0x..`), `down` (default `true`) | A number; the CSV [key names](#key-names) are not accepted here. |
| `POST /gamepad/button` | `index` (1-based), `down` (default `true`) | |
| `POST /gamepad/axis` | `index` (0-based), `value` | The connection's own logical range (see the Gamepad profile's *Axis bits*). |
| `POST /gamepad/dpad` | `up`, `down`, `left`, `right` (each `true`/`false`) | |

## aoa_touch — usage

```
aoa_touch [options] [script.csv]

Device selection:
  --devices <list>        Comma-separated device numbers from the discovery
                           list (e.g. "1,3"), or "all". If omitted, an
                           interactive numbered list is shown.

Profile setup (all explicit; there are no presets):
  --touch-res WxH          Touch surface resolution, e.g. 1080x1920
  --touch-max-contacts N   Max simultaneous touch contacts (1-16, default 16)
  --gamepad-buttons N      Number of gamepad buttons
  --gamepad-axes LIST      Comma-separated axis roles, e.g. x,y,rx,ry
                           (x y z rx ry rz slider dial wheel rudder
                            throttle accelerator brake steering;
                            x and y are required)
  --gamepad-axis-bits N    Bit width per axis, 2-32
  --gamepad-dpad MODE      none|hat|buttons
  --key-usage-range LO,HI  Keyboard HID usage range (default 0x04,0x65)
  --mouse-buttons N        Number of mouse buttons
  --pen-mode MODE          direct|indirect

Playback:
  -A                       Auto-detect touch resolution via `adb shell wm size`
  --speed FACTOR           Playback speed multiplier (default 1.0)
  --loop N                 Stop after N laps, the first included (default: infinite)
  --no-prompt              Do not start the live "-> " offset prompt

ADB Bridge (keeps adb usable while this program holds the phone):
  --adb-port N             First port on 127.0.0.1 (default 6555; the next
                           device gets the next port)
  --no-adb-bridge          Do not start it (the adb server is left alone)
  -h, --help               Show this text
```

The ADB Bridge is the GUI's (see *ADB tab*), but with no tab to switch it,
`aoa_touch` starts it for every device unless `--no-adb-bridge` is given:
the adb server is stopped before the devices are opened, each device's
bridge starts after its HID devices are registered, and
`adb connect 127.0.0.1:<port>` runs in the background while playback starts.
Record from another terminal with `aoa_record -s 127.0.0.1:6555`.

With no script path, the `csv/` folder next to the executable is listed for
interactive selection. A script given on the command line is checked before
any USB work, so a mistake is reported with its file and line straight away.

While playing, type a number of milliseconds at the `->` prompt to shift every
later event (negative is earlier). Ctrl+C stops playback and releases
everything that was held before the devices are closed.

## aoa_record — usage

```
aoa_record [options]

  -o, --output NAME   Recording name or path. A bare name is saved in the
                       csv/ folder next to this program (".csv" is added
                       when missing). Default: record-<date>-<time>
  -s, --serial ID     adb device serial, for `adb -s ID`. A host:port
                       serial, such as the ADB Bridge's 127.0.0.1:6555,
                       is `adb connect`ed first
      --input PATH    Only record /dev/input/eventN; default is every
                       device getevent reports
      --coords MODE   How touch coordinates are written (default: raw)
                       raw         the touch panel's own values, with
                                   "# screen WxH" naming its range
                       normalized  fractions 0..1 of the panel, written with
                                   "@coords normalized"
      --echo          Print each captured row while recording
  -h, --help          Show this text
```

Runs `adb shell getevent -lt`, turns touch (`ABS_MT_*`) and key (`EV_KEY`)
frames into lowercase `t` and `k` rows with measured `wait_ms`, and streams
them to the file. The touch panel's own coordinate range is read with
`getevent -lp`, so playback can scale the recording to whatever touchscreen
resolution is connected:

- `raw` (the default) writes the panel's own values and names its range with
  `# screen WxH`.
- `normalized` writes fractions of the panel (`0.500000` is the middle) under
  `@format 2` and `@coords normalized`.

If the panel's range cannot be read, the recording is written raw and a
warning says so. Ctrl+C stops; the file is flushed and closed first. A
recording that captured nothing leaves no file behind.

## CSV script format

Plain text, no header row, one event per line. `#` starts a comment; blank
lines and a UTF-8 byte-order mark are ignored. Lines starting with `@` are
[directives](#directives).

| prefix | profile        | columns                                     |
|--------|----------------|---------------------------------------------|
| `t`    | touch          | `finger_id,state,x,y,wait_ms`               |
| `m`    | mouse move     | `dx,dy,wait_ms`                             |
| `b`    | mouse button   | `button_no,pressed,wait_ms`                 |
| `k`    | keyboard key   | `usage_or_name,down,wait_ms` (a usage number, `0x..` accepted, or a [key name](#key-names)) |
| `g`    | gamepad button | `button_no,pressed,wait_ms`                 |
| `a`    | gamepad axis   | `axis_index,value,wait_ms`                  |
| `h`    | gamepad dpad   | `up,down,right,left,wait_ms`                |
| `p`    | pen            | `in_range,tip,x,y,pressure,wait_ms`         |

`state`, `down`, `pressed`, `in_range`, `tip`, and the four dpad directions
are `0` or `1`. `axis_index` is the position of the axis in the gamepad axis
list (0-based). `wait_ms` is the delay after the row; rows with `0` are sent
in the same report as the next row. Every row runs at an absolute time
measured from the start of its lap, so a slow USB transfer never makes the
script drift. Keyboard modifiers (usages `0xE0`–`0xE7`) are always available,
whatever the usage range. The keyboard profile is full N-Key Rollover (as of
libaoahid 0.2.0), so every usage in the configured range can be held down at
the same time with no limit on simultaneous keys. See `csv/example.csv`.

### Laps: which rows run when

Rows keep the order they are written in, and the script plays in laps:

- The **first lap** runs every row.
- Each **later lap** skips the *once-only* rows, together with their
  `wait_ms`, so it is shorter by the time those rows took (`keep-time`, below,
  keeps that time).

A row is once-only when its prefix is uppercase (`T,M,B,K,G,A,H,P`) or when it
sits between `@once` and `@end`; lowercase rows outside a block run on every
lap. A once-only row can be anywhere in the file: first, in the middle, or
last. A script whose rows are all once-only plays one lap and ends. *Loops*
(`--loop N` in `aoa_touch`) count laps, the first included; 0 repeats until
stopped.

Earlier versions moved every uppercase row to the front. An uppercase row
written after lowercase rows now runs where it is written; files that keep
their uppercase rows at the top play as before.

### Directives

A directive is a line starting with `@`. Names and options ignore case. Files
without directives are read as before.

| directive | meaning |
|-----------|---------|
| `@format 1` / `@format 2` | Optional. The format version; 1 and 2 are the same language, and anything newer is refused. Before the first row. |
| `@screen WxH` | The size of the coordinate space the touch and pen `x,y` are written in (each from 1 to 65536). The older `# screen WxH` comment still works; `@screen` wins when both are present. Before the first row, once. |
| `@coords normalized` / `@coords integer` | With `normalized`, touch and pen `x,y` are fractions from 0 to 1 (`0.5` is the middle; a value outside 0–1 is an error). The default is `integer`. Pen pressure stays an integer. Before the first row, once; not combined with `@screen`. |
| `@once` … `@end` | Every row inside is once-only, whatever its case. Blocks do not nest. |
| `@once keep-time` … `@end` | The same, and later laps still wait the rows' `wait_ms`, so every lap has the same length. |

### Coordinates

While playing, touch and pen positions are scaled from the space the file
names (`@screen`, `# screen WxH`, or `@coords normalized`) onto the connected
touchscreen and pen surface; without a named space they are used as they are.
A file written in a large square such as `@screen 32768x32768` therefore lands
in the same place on any phone. Each axis is scaled on its own, so a square
space is stretched to a phone that is not square. `@coords normalized` is
stored as a 65536 × 65536 space.

### Key names

`k` rows take a usage number or a name, ignoring case: `A`–`Z`,
`Digit0`–`Digit9`, `F1`–`F24`, `Enter` (`Return`), `Esc` (`Escape`),
`Backspace`, `Tab`, `Space`, `Minus`, `Equal`, `LBracket`, `RBracket`,
`Backslash`, `Semicolon`, `Quote`, `Grave`, `Comma`, `Period`, `Slash`,
`CapsLock`, `PrintScreen`, `ScrollLock`, `Pause`, `Insert`, `Delete`, `Home`,
`End`, `PageUp`, `PageDown`, `Up`, `Down`, `Left`, `Right`, `NumLock`, `Menu`,
and the modifiers `LCtrl`, `LShift`, `LAlt`, `LGui`, `RCtrl`, `RShift`,
`RAlt`, `RGui` (`Ctrl`, `Shift`, `Alt`, and `Gui`, `Win`, `Meta`, `Super` mean
the left one). A bare number is always a usage number, so write digit keys as
`Digit1`.

### Checks

- Every problem in a file is reported with its file and line, up to 50 (then
  reading stops). A file with any problem is not played.
- **Lap warning:** laps from the third on start where the second ended. When
  the repeat lap leaves a control in a different state than it found it (usually
  a once-only row releases what the repeated rows press, or the reverse), the
  laps start differently from the second, and the control is named in a
  warning. Scripts with no once-only rows never warn.

## Linux permissions (udev)

The programs open the phone's USB device directly, so the user running them
needs write access to it. The logged-in user often has it already; if
connecting fails with an access error, install the rule shipped in this
repository (the same one libaoahid ships), then unplug and replug the phone:

```sh
sudo cp udev/51-aoahid.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

The phone keeps its own vendor and product IDs, so the rule gives the user at
the local seat access to USB devices through systemd-logind's `uaccess` tag
rather than matching one ID. The comments in
[`udev/51-aoahid.rules`](udev/51-aoahid.rules) show a group-based rule for
one phone instead, for systems without logind.

## Building from source

### Requirements

- CMake 3.21+ and a C++20 compiler (GCC or Clang on Linux, MSVC on Windows)
- [libaoahid](https://github.com/nemarpuc/libaoahid) 4.0.0 or newer 4.x
  (see below)
- For the GUI on Linux, the X11 and Wayland development headers GLFW builds
  against, for example on Debian/Ubuntu:
  `libwayland-dev libxkbcommon-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev pkg-config`
  (Arch/CachyOS: `wayland libxkbcommon libx11 libxrandr libxinerama libxcursor libxi libxext`).
  At run time GLFW loads X11 or Wayland and OpenGL itself, so the GUI runs
  on either.
- Network access at configure time for the GUI: Dear ImGui 1.92.9, GLFW
  3.5.1, and cpp-httplib 0.57.1 (the control API's HTTP server; see
  [Control API](#control-api)) are downloaded at pinned, SHA-256-checked
  releases; ImGui and GLFW are linked statically, cpp-httplib is header-only.
  To build offline, pass
  `-DFETCHCONTENT_SOURCE_DIR_IMGUI=<dir> -DFETCHCONTENT_SOURCE_DIR_GLFW=<dir> -DFETCHCONTENT_SOURCE_DIR_HTTPLIB=<dir>`
  pointing at local copies of those releases, or `-DAOAHID_PLAYER_BUILD_GUI=OFF`.

### Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/libaoahid
cmake --build build --config Release
```

On Linux, `make AOAHID_PREFIX=/path/to/libaoahid` does the same first
configure and build in one step (`make` alone once configured); `make test`
and `make run` build first, then run the unit tests or launch the GUI.
`make help` lists every target. It is a thin wrapper around the commands
above — nothing it does is unavailable through CMake directly — and it is
not meant for Windows, which has no `make` by default; use the Visual
Studio generator there (`windows-release`, or `windows-arm64-release` for
ARM64, in `CMakePresets.json`).

Every program lands in `build/out/<config>/`, next to a copy of `csv/` (and,
on Windows, the libaoahid DLLs), so it runs straight from there. To lay out
the same folder the release archive contains:

```sh
cmake --install build --config Release --component runtime --prefix aoahid-player
```

Options: `AOAHID_PLAYER_BUILD_GUI` (default `ON`) and
`AOAHID_PLAYER_BUNDLE_RUNTIME` (default `ON`: install libaoahid, libusb and,
with MSVC, the C++ runtime next to the programs).

### Getting libaoahid

`find_package(aoahid CONFIG REQUIRED)` fails immediately if CMake cannot
find libaoahid. It is not packaged by distributions or vcpkg/conan, so get it
one of these two ways.

**Option A — prebuilt release (fastest, no libusb build needed):** download
the shared-library archive for your platform from
[libaoahid's releases](https://github.com/nemarpuc/libaoahid/releases)
(the same asset the workflows here use), extract it, and point
`CMAKE_PREFIX_PATH` at the extracted directory:

```sh
curl -LO https://github.com/nemarpuc/libaoahid/releases/latest/download/libaoahid-<version>-linux-x86_64-ubuntu22.04-shared.tar.gz
tar -xzf libaoahid-<version>-linux-x86_64-ubuntu22.04-shared.tar.gz
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/libaoahid-<version>-linux-x86_64-ubuntu22.04-shared"
```

**Option B — build libaoahid from source:**

```sh
git clone https://github.com/nemarpuc/libaoahid.git
cmake -S libaoahid -B libaoahid/build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/libaoahid-staging"
cmake --build libaoahid/build --config Release
cmake --install libaoahid/build
```

libaoahid requires libusb 1.0.30 or newer (`LIBUSB_API_VERSION >=
0x0100010C`). Many distributions ship older versions (Ubuntu 24.04 has 1.0.27),
so building from source there also means building libusb 1.0.30 first and
pointing `PKG_CONFIG_PATH` at it. The prebuilt archives already bundle a
matching libusb runtime.

### Testing

```sh
ctest --test-dir build --output-on-failure -C Release
```

Unit tests cover CSV parsing (rows, directives, key names, error reports), the
two-lap script timeline and coordinate scaling, how recordings write
coordinates, `adb shell getevent` parsing, HID descriptor bit widths, and the
input-state transitions used for stop, pause, and seek, including the lap
warnings. They use [doctest](https://github.com/doctest/doctest),
vendored under `tests/third_party/`. They are built only when this is the
top-level project; pass `-DBUILD_TESTING=OFF` to skip them.

## Using `aoahid_player_core` as a library

The engine behind all three programs is a static library with no console
I/O: status and errors reach the caller through an `EventSink`. The
`development` install component exports it as a CMake package:

```sh
cmake --install build --config Release --component development --prefix /path/to/staging
```

```cmake
find_package(aoahid_player_core CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE aoahid_player::aoahid_player_core)
```

The main headers in `include/aoahid_player/`:

- `session.hpp` — device discovery, connect and disconnect with a
  `ProfileSetup` and optional `AdbBridgeOptions` (the ADB Bridge, owned by
  each `Device` in `device.hpp`).
- `player.hpp` — playback with pause, seek, speed, loop limit, live offset,
  and a wall-clock stop time, all callable from any thread; `status()` is
  lock-free, and a `PlaybackObserver` sees every row sent. `set_live_pump()`
  lets another input source (the GUI's Live tab) interleave into the same
  `DeviceGroup` while a script plays; `wake_live()` wakes it promptly.
- `recorder.hpp` — `adb getevent` recording into a CSV file, with the raw and
  normalized coordinate modes.
- `event_script.hpp` — CSV loading (rows in file order, directives, key names)
  and the two-lap script timeline.
- `adb.hpp`, `paths.hpp`, `events.hpp` — adb helpers, UTF-8 paths and the
  `csv/` folder, and the `EventSink` interface.

`CMAKE_PREFIX_PATH` needs both this package's prefix and libaoahid's.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| Windows: the ADB Bridge does not start, and the error contains `libusb status -12` | The phone's adb interface uses a driver that libusb cannot use. Some manufacturers install their own USB driver by default. | Switch the phone to WinUSB with Zadig ([example below](#example-samsung-on-windows)). |

### Example: Samsung on Windows

A Samsung tablet on Windows, with Samsung's USB driver installed as it is by
default, connected and played scripts normally, but its ADB Bridge would not
start. The *ADB* tab showed:

```text
Could not start: the phone's ADB interface could not be opened:
AOAHID_ERR_UNSUPPORTED (channel.open: The Bulk interface could not be read,
claimed, or started.), libusb status -12. USB debugging may be off, or another
program holds the interface.
```

That is the 0.18.2 message. From 0.18.3, a `-12` ends with "The phone's adb
interface needs a driver libusb can use, such as WinUSB" instead of the USB
debugging hint, and before 0.18.2 the message was only "the phone has no free
ADB interface (USB debugging is off, or an adb server still holds it)".

`-12` is libusb's `LIBUSB_ERROR_NOT_SUPPORTED`. On Windows, libusb can only
claim an interface whose driver is WinUSB, libusbK or libusb0. HID still
worked because it goes through the phone's control endpoint, not the adb
interface. USB debugging and the adb server had nothing to do with it.

Why Samsung and not the HyperOS phone: on Windows the HyperOS phone's adb
interface came up with WinUSB from the start, so the bridge worked with no
changes. Samsung ships its own dedicated USB driver, and Windows uses it for
Samsung devices instead of WinUSB. On the tablet, the parent device "SAMSUNG
Mobile USB Composite Device" used Samsung's `dg_ssudbus` (version 2.21.4.0).
libusb accepts `dg_ssudbus` as a composite parent, but the adb interface under
it did not get WinUSB, so libusb could not claim it.

The fix was to remove Samsung's driver and give the phone WinUSB:

1. Plug in the phone and open Device Manager.
2. Under *Universal Serial Bus controllers*, right-click the phone's parent
   device ("SAMSUNG Mobile USB Composite Device" for Samsung) and choose
   *Uninstall device*. Tick *Attempt to remove the driver for this device*
   (*Delete the driver software for this device* on Windows 10) and click
   *Uninstall*. Without this tick, Windows puts the same driver back.
3. Unplug the phone and plug it back in.
4. Download and run [Zadig](https://zadig.akeo.ie/).
5. Turn on *Options → List All Devices*.
6. Pick the entry for the whole phone, not one of its interfaces. On the
   Samsung tablet it was "SAMSUNG Android"; the entries for its adb and MTP
   interfaces (names ending in "(Interface N)") did not fix it when changed.
   The names depend on the device; the whole phone's *USB ID* has two boxes
   (vendor and product), while an interface entry has a third one.
7. Set the driver on the right of the arrow to *WinUSB* and click
   *Replace Driver* (*Install Driver* if it had none).
8. Connect the phone in this program again and turn its bridge on.

With the whole phone on WinUSB, libusb treats it as one WinUSB device and
reaches every interface, adb included, through it. Replacing only the adb
interface's driver left the bridge failing on the Samsung tablet, most
likely because Samsung's composite driver does not expose that interface in
a way libusb can map; this was not examined further.

Other manufacturers that ship their own dedicated USB driver can be handled
the same way. A phone whose adb interface is already WinUSB, like the HyperOS
phone, needs none of this.

After the change, Windows `adb` on its own no longer sees the phone over USB,
Windows no longer shows the phone for file transfer (MTP), since the whole
phone is on WinUSB, and tools that need the manufacturer's driver (such as
Samsung Smart Switch) may stop working with it. Use `adb` through the ADB Bridge. To undo, repeat
steps 1-3, or reinstall the manufacturer's USB driver.

## Known limitations

- A script has two kinds of rows, once-only and repeated; there is no
  per-row condition such as "on lap 3" or "every fifth lap".
- Recording captures touches and keyboard keys only, not mouse, gamepad, or
  pen input.
- Recording tracks the multi-touch Type B protocol (`ABS_MT_SLOT` plus
  `ABS_MT_TRACKING_ID`); the older Type A `SYN_MT_REPORT` form is not parsed.
- libaoahid's battery and raw profiles are not exposed.

## License

MIT; see [LICENSE](LICENSE). The release archives also contain, each under
its own licence in `third-party/`:

- [libaoahid](https://github.com/nemarpuc/libaoahid) (MIT) and libusb-1.0
  (LGPL-2.1-or-later, dynamically linked, with its licence and corresponding
  source), as shipped by libaoahid, under `third-party/libaoahid/`
- [Dear ImGui](https://github.com/ocornut/imgui) (MIT) and
  [GLFW](https://www.glfw.org/) (zlib), linked into the GUI
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) (MIT), header-only,
  backing the control API's HTTP server
- [aoahid_adb_proxy](https://github.com/nemarpuc/aoahid_adb_proxy) 2.1.0
  (MIT), vendored under `third_party/aoahid_adb_proxy/` for the ADB Bridge
- the Roboto font (Apache-2.0), embedded in the GUI
- [stb_image](https://github.com/nothings/stb) (MIT/public domain), vendored
  for the Live tab's reference image
- on Windows, the Microsoft Visual C++ runtime DLLs, redistributed under
  Microsoft's redistribution terms

The test suite vendors doctest (MIT, `tests/third_party/LICENSE-doctest.txt`);
it is not linked into any program.
