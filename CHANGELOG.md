# Changelog

All notable changes to aoahid_player are recorded here. Entries before 0.20.2
were reconstructed from the commit history.

## [Unreleased]

## [1.1.0] - 2026-10-04

Fixes from a review of the three repositories; the full list, with what is
still open, is in `docs/REVIEW_2026-10.md`. Checked with the test suite and
against libaoahid's fake USB backend, not on a phone.

### Fixed

- Connecting waits 100 ms after the HID devices are registered, so the first
  input is not sent before Android has finished creating them. `aoa_touch`
  started playing at once, and a first row that arrived too early was refused
  and the phone dropped.
- A touch row that needs more contacts than the profile declares is skipped
  with a warning. It dropped the phone ("stopped responding").
- A Stop pressed just as a script or a playlist step ends no longer makes the
  next playback stop as soon as it starts.
- Live input queued while a script waits for its next row, or is paused, is
  sent at once; it could wait for the next row.
- Recording: an existing script is replaced only when the new recording stops
  with something captured. A recording that could not start, or captured
  nothing, deleted the script of the same name. Rows are written to
  `<name>.part` until then.
- Recording: events for a touch slot above the 16 tracked ones are dropped.
  They were applied to the slot selected before.
- `aoa_touch`: the offset prompt ignores `nan`, `inf`, and steps beyond 1e9 ms.
- GUI: the status row at the top right lines up with the cards' edge; it ran
  past it, and into the window edge while recording.
- GUI: the settings file is no longer rewritten on every frame while the
  window is moved or resized; `nan` in it is ignored for a number.

### Changed

- Control API: a boolean parameter that is given must be `true`/`false`,
  `1`/`0`, `on`/`off`, or `yes`/`no`, or the route answers 400. A typo such as
  `down=flase` used to press the key. A parameter that is left out still takes
  its default.
- Control API: `/play` answers 409 when another request started a playback
  first; two simultaneous requests could both be queued. `/seek` answers 400
  for a `time_ms` too large to convert.
- Control API: the port is bound for this program alone. On Linux a second
  program could bind the same port and receive part of the requests.
- The vendored aoahid_adb_proxy is 3.1.2: when the phone is lost, a new
  `adb connect` to the bridge's port fails at once instead of waiting for an
  answer that never comes. The port stays bound until the bridge is stopped.
- CI and release builds link libaoahid 4.0.4 (was 4.0.3).
- `aoahid_player_core`: `Player::discard_requests()`, `partial_record_path()`,
  and `commit_recording()` are new.

### Documentation

- Added `docs/ARCHITECTURE.md`, `docs/CROSS_REPO.md`, and
  `docs/REVIEW_2026-10.md`.

## [1.0.2] - 2026-10-03

- Update the vendored aoahid_adb_proxy to 3.1.0. The bridge behaves the same;
  the proxy's result codes now have names (`AOAHID_ADB_PROXY_ERR_*`) and the
  bridge code uses them instead of bare numbers.
- CI and release builds link libaoahid 4.0.3 (was 4.0.2), a maintenance
  release with no behavior change.
- The control API's default port (47821) is defined once.

## [1.0.1] - 2026-10-03

- GUI glass look: the primary keys (Connect and the like) are now translucent
  and follow the Glass *Clear* setting instead of a fixed opacity; their text
  stays light over a dark window. *Accessory Mode* is a quiet key so Connect
  leads.
- Background picture: a preview in the Background card shows the picture with
  the dim in effect, and *Auto dim for readability* (on by default, saved as
  `ui.bg_auto_dim`) raises the dim on a picture too bright for the dark theme
  (or too dark for the light one). It never lowers the *Dim* you set.

## [1.0.0] - 2026-10-03

First stable release.

- Control API: `/seek` no longer overflows on a very large `time_ms`;
  `/mouse/move` and `/mouse/wheel` answer 400 for a value that is not a
  number instead of silently sending zero.

## [0.22.0] - 2026-10-03

- GUI look: solid by default with a high-contrast dark theme (#000000 background)
  and electric blue accent (#388BFD). Cards, popups, and keys are opaque with
  sharp 12 px rounding.
- Background image auto-preset: setting a background picture automatically activates
  the glass look, lavender accent (#A78BFA), 24 px card rounding, 1 px blur,
  25% dim, and 75% live phone glass.
- Glass UI widgets: translucent buttons, toggles, checkboxes, and sliders with
  hairline rims and gloss; glass clear range expanded to 90%.
- Sidebar: removed the outer card wrapper from Connect / Accessory Mode
  buttons to eliminate double borders and streamline layout.
- Image decoding: unsupported image formats (WebP, AVIF, HEIC, TIFF, JPEG XL,
  SVG, PDF) report the actual format and provide a clear remediation message
  when selected as a background or overlay image.
- Background: when the glass look is off, the precomputed blur texture for
  background images is skipped and freed to conserve memory.

- GUI look: glass. Cards, popups, tooltips, and keys are translucent with a
  gloss and a lit rim; the accent is light purple. With a background image,
  the cards show a blurred copy of what is behind them (computed once when
  the image loads, not per frame).
- New *Settings* tab, all of it saved in `aoahid_player_gui.ini`:
  - *Presets*: the whole look saved under a name as `themes/<name>.theme`,
    applied or deleted from a list.
  - *Appearance*: Dark or Light (moved here from the header), the accent
    colour, the UI size (Small to Larger), and a short fade on tab changes.
  - *Glass*: how see-through the cards are, the gloss and rim strength, and
    the cards' corner radius.
  - *Background*: the theme's colour, a colour, or an image (.png/.jpg/.bmp,
    chosen with the system's file picker, typed, or dropped on the window
    outside the Live tab), with *Blur* and *Dim*.
  - *Live preview*: the phone's screen can be glass instead of black, with
    its own clearness and corner radius; the reference image's settings
    moved here from the Live tab (its lock stays on the Live tab too, and is
    now saved).
  - *Layout*: the sidebar on the left or right, the Activity panel shown or
    hidden, and notifications.
- Notifications: warnings, errors, connecting, and disconnecting show for a
  few seconds in a bottom corner.
- Live tab: the window's background shows around the phone instead of a
  black panel.
- Building the GUI now also fetches nativefiledialog-extended 1.4.1 (zlib,
  linked statically); on Linux it needs `libdbus-1` (the file picker goes
  through xdg-desktop-portal).
- Toggle profile: `BrightnessUp` (`0x6F`) and `BrightnessDown` (`0x70`) join
  the media keys, in `c` rows and on the Live tab. Not yet tested on a device.
- Live tab: the phone's side keys sit beside the preview (*Volume* + and −
  with *Mute*, *Brightness* + and −; volume and brightness stay down while
  held), the media keys under it (*Prev*, *Play/Pause*, *Next*), and *All
  keys* opens every Toggle key. The switches, *Paste Text*, *Rotate*, and the
  input log moved to a panel on the right; *Mouse release key* and *Shape*
  are folded rows there. Full screen keeps 5 px around the
  phone and draws its edge on all four sides.
- Player tab: the Playlist tab is now the Player's *Playlist* mode (the
  *Script | Playlist* switch), sharing one play bar; pause, seek, speed, and
  offset act on the script that is running. `.csv` files dropped on the
  Playlist join its list. A saved Playlist tab opens in this mode. Saved
  playlists open from a searchable list like the script one. The key settings
  are a folded *Keys* row.
- Profiles: each row is a switch, a summary, and an arrow that opens its
  settings. A touchscreen size that could not be read from the phone is shown
  in the warning colour. *Connect* needs at least one profile on.
- Defaults: every profile starts off; Live forwards Touch and Keyboard; the
  touchscreen size is 1440 x 2560 until it is read from the phone; the app
  opens on the Live tab. Saved settings are not changed.
- Live: the window handles events while it waits out the frame-rate cap, so
  pointer motion (mouse moves, a touch being dragged) and key releases reach
  the phone when they arrive instead of up to one frame later (8.3 ms at
  120 fps). Key presses, button presses, and the start and end of a touch
  still go out with the next frame.
- Tabs are ordered Live, Player, Recorder, ADB.
- ADB and Recorder: the long explanations became one line with the detail in
  a tooltip.

## [0.20.5] - 2026-09-30

- Update the vendored aoahid_adb_proxy to 3.0.3. The bridge code is
  unchanged; the vendored header in 0.20.4 still reported version 3.0.1.
- CI and release builds link libaoahid 4.0.2 (was 4.0.0), which no longer spins
  in `aoahid_node_submit_blocking` when every transfer slot is in use.

## [0.20.4] - 2026-09-30

- CSV: `c` rows send media keys through the toggle profile
  (`c,usage_or_name,down,wait_ms`), by name (`VolumeUp`, `PlayPause`, ...) or
  Consumer usage. Stop, pause, and seek release a held media key like any
  other control. `aoa_touch --toggle` enables the profile.
- Control API: `POST /refresh`, `/connect`, `/disconnect`, `/record/start`,
  `/record/stop`, `/bridge`, `/pen`, and `/media`; `/key` also takes key
  names; `GET /status` adds `available`, `bridges`, `recording`, `active`,
  and `busy`.
- Control API: window routes keep working while the window is minimized;
  `/connect` answers 409 while the device list is still being prepared;
  numbers with a leading zero are decimal (they were octal); an invalid
  `/play` `loop` is a 400 instead of repeating forever; `/record/start`
  reports an invalid name or missing adb instead of a misleading 400.
- Recorder: touchscreens' BTN_TOUCH and BTN_TOOL_* are no longer reported as
  skipped keys.
- Vendored aoahid_adb_proxy: a payload whose header already reached the
  phone is always written, so a bridge client disconnecting mid-transfer no
  longer desynchronises adbd.
- A device dropped for a STALL is reported as refusing a report (usually
  input sent while Android is still registering the device), not as
  "stopped responding".

## [0.20.3] - 2026-09-29

- GUI: the Speed and Loops fields (and every other text field) take typed
  input while Live control forwards keys. Before, the forwarding cleared all
  keyboard input every frame, so nothing could be typed.
- GUI: in Live mouse mode, only a click on the preview captures the pointer.
  Before, a mouse button a script was holding made a click anywhere in the
  window capture it.
- README: a demo video.
- The vendored aoahid_adb_proxy is 3.0.1 (documentation-only release).
- README: Windows `adb` keeps seeing the phone after its driver is switched to
  WinUSB; the README said it no longer did. Only tools that need the
  manufacturer's driver (and possibly MTP) are affected.
- README: record the hardware the ADB Bridge was verified on (Galaxy Tab S11
  and POCO F6 Pro, on Windows 10 x64 and Arch Linux, with WinUSB and libusbK).

## [0.20.2] - 2026-09-28

- Upload release archives uncompressed

## [0.20.1] - 2026-09-28

- List only AOA 2.0 devices

## [0.20.0] - 2026-09-28

- Use libaoahid 4.0.0 and aoahid_adb_proxy 3.0.0

## [0.19.1] - 2026-09-28

- Restart the adb server after an ADB Bridge stops or Disconnect

## [0.19.0] - 2026-09-28

- Fix toggle-only connections and harden the control API
- Add Windows ARM64 packages

## [0.18.4] - 2026-09-27

- Use the renamed libaoahid repository and 3.0.4

## [0.18.3] - 2026-09-27

- Give each platform its own device and bridge hints
- README: add Troubleshooting with the Samsung-on-Windows driver fix (Zadig on the whole phone)

## [0.18.2] - 2026-09-27

- Use libaoahid 3.0.2
- Show libaoahid's reason when the ADB Bridge cannot open the interface

## [0.18.1] - 2026-09-27

- Use libaoahid 3.0.1; keep its notices in `third-party/libaoahid`

## [0.18.0] - 2026-09-27

- Add ADB Bridge: use adb while the phone is connected over AOA

## [0.17.6] - 2026-09-23

- Fix the AC Pan button (0x0238) being dropped by the Toggle profile

## [0.17.5] - 2026-09-23

- Fix a build error from a missing `<cstring>` include

## [0.17.4] - 2026-09-23

- Add Toggle profile setting and display USB bus/port in devices list

## [0.17.3] - 2026-09-23

- Documentation updates

## [0.17.2] - 2026-09-23

- Update live toggle UI and add missing consumer control usages

## [0.17.1] - 2026-09-23

- Fix `AOAHID_ERR_NO_DEVICE` when starting accessory mode

## [0.17.0] - 2026-09-23

- Add accessory mode start and Toggle features

## [0.16.1] - 2026-09-22

- Fix the control API's silent no-ops, and move the AOA link card to Recorder

## [0.16.0] - 2026-09-22

- Read touch resolution via `wm size`, and fix normalized-coordinate rounding

## [0.15.0] - 2026-09-22

- Remove the Media keys and Power/Sleep/Wake profiles

## [0.14.0] - 2026-09-22

- Add a local HTTP control API for driving the app from another program
- Add a Media keys profile (volume, mute, play/pause, track, stop)
- Add a convenience Makefile

## [0.13.0] - 2026-09-22

- Let Live control run alongside playback, and speed up its touch tracking

## [0.12.1] - 2026-09-21

- Stop keys consumed by the input method from reaching the app on X11

## [0.12.0] - 2026-09-21

- Allow IME input by opening under XWayland, and add remappable Player keys

## [0.11.0] - 2026-09-20

- Remove the virtual recording mode and the compatibility badges

## [0.10.1] - 2026-09-20

- Document once-only rows anywhere, script format 2, and the recorder coordinate modes

## [0.10.0] - 2026-09-20

- Allow once-only rows anywhere in a script; add script format 2 and recorder coordinate modes

## [0.9.0] - 2026-09-20

- Move Live full screen controls off the phone; do not forward the pointer-grab click
- Add an AOA connection card to the Player tab; stop the full screen bar covering taps

## [0.8.1] - 2026-09-20

- Require libaoahid 0.5.3 for the Gamepad byte-span fix

## [0.8.0] - 2026-09-19

- Redesign the GUI: icon rail, dual-accent themes, floating live controls

## [0.7.0] - 2026-09-16

- Modern UI pass: latency, persistence, UX, and a fluid segmented-control feel

## [0.6.0] - 2026-09-16

- Improve GUI readability: text contrast, row truncation, resizable sidebar

## [0.5.0] - 2026-09-16

- Fix libaoahid 0.5.0 touchscreen options rename

## [0.4.0] - 2026-09-16

- Build against libaoahid 0.4.0 (no functional changes)

## [0.3.0] - 2026-09-15

- Fix libaoahid 0.3.0 gamepad break; hex-only usage settings; real full screen

## [0.2.1] - 2026-09-14

- Show 0x prefix on keyboard usage range inputs in GUI

## [0.2.0] - 2026-09-14

- Update keyboard profile for libaoahid 0.2.0

## [0.1.0] - 2026-09-14

- Initial release
