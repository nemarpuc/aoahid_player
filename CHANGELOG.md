# Changelog

All notable changes to aoahid_player are recorded here. Entries before 0.20.2
were reconstructed from the commit history.

## [Unreleased]

## [3.3.2] - 2026-10-29

- Builds against libaoahid 2.0.0 as re-released on 2026-10-29. The Device starts from
  `aoahid_device_options_init` (the same Linux HID limits as before) and the accessory request
  passes its wait to `aoahid_accessory_start`. A Node whose close fails is now dropped, since
  libaoahid consumes it with every result but `AOAHID_ERR_PARAM`, `AOAHID_ERR_UNSET_FIELD` and
  `AOAHID_CLOSE_PENDING`. Not run against a phone.

## [3.3.1] - 2026-10-27

- Builds against libaoahid 2.0.0 and aoahid_adb_proxy 4.0.0, which take every
  wait as an argument of the call that waits. The player's waits (500 ms for a
  control request and for a report, 1000 ms to drain a close) are the same as
  before; they are now in `waits.hpp` and passed to each call instead of being
  set in the Device options. Nothing changes for the user.
- Not run against a phone.

## [3.3.0] - 2026-10-12

- Playlist: *Loops* under the list sets how many times the whole list plays;
  0 repeats it until stopped, as for a single script. It is saved in the
  playlist file as `loops = N`; a file without the line plays once, as
  before. The play bar shows which loop of the list is running.

## [3.2.1] - 2026-10-12

- The Script card's refresh button also reads the loaded script again (not
  while something is playing), so a file can be edited and tried without
  choosing it anew.

## [3.2.0] - 2026-10-12

- Playlist: *Add a script* opens the same searchable picker as Script mode
  (type to search, Up/Down, Enter adds), in place of a plain drop-down of
  every file in the csv folder.

## [3.1.2] - 2026-10-12

- Player tab: the list of touches currently down is the last thing in the
  card. It sat above Speed, Loops, Lap and Offset, so those controls moved up
  and down with every touch of a playing script.

## [3.1.1] - 2026-10-12

- The messages for a row or directive the script format does not have say what
  the format is, without naming earlier formats: `@format must be 3`, `row
  prefixes are lowercase`, `unknown directive @once`, and `the screen size is
  a directive, not a comment; write @screen WxH`. The README section
  "Migrating from format 1 and 2" is gone.

## [3.1.0] - 2026-10-11

- **A HID can report a battery level.** Every HID on the Devices tab has a
  Battery switch with a Level and a Range; the level is sent as a second
  report of that HID, which is how Android shows a battery for an input
  device. Each device keeps its own (`<hid>.battery`, `.battery.min`,
  `.battery.max`, `.battery.level` in its section of the ini). Changing only
  the level never registers the HID again.
- Control API: `POST /battery` sets one HID's battery on a connected device,
  and each device in `GET /status` has `battery`, the level of every HID that
  reports one.
- Builds against libaoahid 1.0.0 as re-released on 2026-10-11 (the battery is
  an option of each profile there, and the profile numbers changed). A build
  of 3.0.1 cannot open its HIDs with that library, nor this build with the
  earlier one: the options structures differ in size.
- Fixed: the Devices tab kept the Toggle row's "settings shown" flag one past
  the end of its array.

## [3.0.1] - 2026-10-11

- Builds against libaoahid 1.0.0 as it is released. Only names changed: the
  media key call is `aoahid::toggle_usage` (it was `aoahid::toggle`), the gamepad
  axis call is `aoahid::gamepad_axis` (it was `aoahid::gamepad_set_axis`), and
  the options fields are `contact_id`, `aoahid_gamepad_axis_option`,
  `linux_hid_report_data_policy_bits`, and `linux_hid_report_size_policy_bits`.
  Behavior is the same.

## [3.0.0] - 2026-10-11

- **Media keys can be held together.** libaoahid's toggle profile now keeps one
  bit per Usage, so two `c` rows (or a script row and a Live button) can hold
  two keys at once; each release names its own key. Before, a press replaced
  the key held and any release cleared it. Seeking and stopping release every
  held key.
- **Digitizer profile (breaking).** libaoahid 1.0.0 split the stylus in two,
  so the player has a *Digitizer* HID next to *Pen*: a `d` CSV row (the
  columns of `p`), `POST /digitizer`, the `digitizer` name for `POST /hid`,
  `--digitizer` and `--pen` for `aoa_touch`, and a *Digitizer* switch on the
  Devices tab. `--pen-mode direct|indirect` and the pen's *Mode* setting are
  gone: use *Pen* for the old direct form and *Digitizer* for the indirect
  form. A saved `pen.mode = indirect` is read as the Digitizer switch.
  Builds against libaoahid 1.0.0 or newer.
- **HIDs can be added and removed while connected.** A device's switches on
  the new Devices tab act at once, and changed settings are registered again
  with *Apply*: while stopped, paused, or in Live control, not while a script
  is playing. A device may have no HID at all and stay connected.
- **Settings per device (breaking).** Every device has its own HIDs and
  settings, saved in a `[device <serial>]` section of
  `aoahid_player_gui.ini`; a device seen for the first time copies
  `[device default]`. The profile settings of an older file are not read. The
  startup adb read sets each phone's own resolution and leaves a typed one
  alone.
- Coordinates are scaled for each device as a row is sent, so devices with
  different resolutions play one file. A row goes to the devices that have
  its HID; a device it does not fit skips it and the others still get it.
- GUI: a Devices tab (the device list, Connect, and the shown device's HIDs)
  replaces the left panel; the app opens on it. The Control API switch moved
  to the Settings tab. The sidebar's width, side, and collapsed state, and
  the remembered tab, are no longer settings.
- Control API (breaking): `/status` reports `device_count` and a `devices`
  array (label, active, bridge, reports, skipped, profiles, touch) in place
  of the `devices` count and the top-level `profiles` and `bridges`, plus the
  `space` `/touch` and `/pen` positions are given in. New `POST /hid` adds or
  removes one HID on one connected device. `/connect` uses each device's
  saved settings.
- Core (breaking): `Session::connect()` takes one `ProfileSetup` per device;
  `Session::reconfigure()` and `DeviceGroup::reconfigure()` change a
  connected device; `DeviceGroup::apply()` takes the `Space` of the row;
  `build_spec()` replaces `SpecSet`; `scale_script()` is gone
  (`scale_coordinate()` is public); a setup with no profile is valid.
  `DeviceGroup::scroll()` and `toggle()` skip a device that refuses the
  value instead of dropping it.

## [2.0.0] - 2026-10-10

- **Script format 3 (breaking).** Rows can be put in lap blocks that choose
  the laps they run on: `@lap A`, `@lap A..B`, `@lap A..`, `@every N [from A]
  [to B]`, with `@else` for the other laps and `keep-time` as before. A `w`
  row only waits, and the trailing `wait_ms` of every row can be left out.
- Removed from the format: uppercase row prefixes, `@once` (write `@lap 1`),
  the `# screen WxH` comment (write `@screen WxH`), and `@format 1` / `2`.
  Each is refused with the line number and what to write instead; see
  "Migrating from format 1 and 2" in the README. Files made only of
  lowercase rows play as before.
- Playback ends when no later lap has a row left, and a seek rebuilds the
  exact input state on any lap. The lap warning is gone with the
  approximation it covered.
- GUI: the Player tab's bar shows the lap being played, the label reads
  *Lap k of N*, and a *Lap* field jumps to any lap.
- Control API (breaking): `lap` is a number counted from 1 in `/status` and
  `/seek` (was `first` / `repeat`); `/seek` without `lap` stays in the lap
  being played.
- `aoa_record` writes `@screen WxH` (raw) and `@format 3` (normalized).

## [1.1.3] - 2026-10-09

- Builds against libaoahid 4.2.0 (the minimum is now 4.2.0, and CI and release
  builds link it). libaoahid 4.2.0 removed the typed node references from
  `aoahid.hpp`; the player now calls the `aoahid::` functions on the Node
  pointers it already keeps, so `Device` no longer holds a typed reference per
  profile. Nothing changes for a user.

## [1.1.2] - 2026-10-04

- CI and release builds link libaoahid 4.0.5 (was 4.0.4). It takes two
  internal 4.0.4 changes out again and keeps the field-width fix; nothing
  changes for this program. The docs follow.

## [1.1.1] - 2026-10-04

- The vendored aoahid_adb_proxy is 3.1.3. When the phone is lost the ADB
  Bridge closes its port, so `adb connect` is refused. 3.1.2 kept the port
  bound and closed each new connection instead; 3.1.3 went back to the simpler
  behavior, which needs no extra state. Nothing else changed.

## [1.1.0] - 2026-10-04

Fixes from a review of the three repositories. Checked with the test suite and
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

- Added `docs/ARCHITECTURE.md` and `docs/CROSS_REPO.md`.

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
