# The GUI

Start `aoahid_player_gui`. The settings are saved to
`aoahid_player_gui.ini` next to the program whenever you finish changing them,
and restored at the next start; delete the file to go back to the defaults.
If the folder is read-only, changes last until the app closes. Each device's
HIDs and their settings are kept in a section of their own, `[device <serial>]`
(a phone that reports no serial is known by its USB address instead, so its
settings are not found again once it is plugged in elsewhere, and two phones
with the same serial share one section). A file written by an
earlier version of the program keeps its window settings; its profile settings are not read.

On startup, before the first device scan, the app starts the adb server,
reads every phone's touchscreen resolution with `adb shell wm size`, and
stops the server again (it can otherwise hold the phones' USB interfaces and
block the accessory handshake) — all once, so Connect itself never has to
wait on adb. An *adb* status pill next to the connection status (top right)
shows whether the server is running, stopped, not found, or not yet known,
and each change is logged.

The icons on the left switch between the tabs: Devices, Live, Player,
Recorder, ADB, and Settings. The app opens on Devices.

**Devices tab**

1. The list on the left holds *Default* and every AOA-capable phone found.
   The refresh button stops the adb server (in case something else started
   it since) and searches again, without touching the screen size or
   connecting on its own. Clicking a row shows that device on the right;
   ticking its box chooses it for Connect (a single phone is ticked for you).
2. The right side shows the HIDs the device presents: touchscreen, mouse,
   keyboard, gamepad, pen, digitizer, toggle. Touchscreen, mouse, and keyboard
   start on.
   Each row has a switch, a one-line summary, and an arrow that opens that
   HID's settings (they start folded); hover a name to see what it is for.
   Every device has its own switches and settings. A phone seen for the
   first time starts with a copy of *Default*; changing *Default* later does
   not change the phones already listed.
   The touchscreen resolution comes from the startup adb read described above
   (an override size wins, because that is what touches map to); type into
   the width/height fields to set it by hand instead, and a typed size is
   kept at the next start. When the read failed,
   the size is shown in the warning colour until it is typed in, because
   touches land in the wrong place if it does not match the phone. *Contacts* (16 by
   default, the maximum) is how many simultaneous fingers the touchscreen
   declares; the Live tab always uses the last one for its own pointer (see
   *Live tab*, below), so a script gets one fewer than this to itself.
3. *Connect* (below the list, available once a ticked device has at least one
   HID on) starts the AOA handshake on the ticked devices right away, each
   with its own settings —
   it does not touch adb or rescan, so it stays fast how ever many devices
   are ticked. *Accessory Mode* switches the ticked devices into AOA
   accessory mode without taking the connection; to leave this mode
   afterwards, physically unplug and replug the USB cable.

Every HID has a **Battery** switch in its settings. With it on, that HID
reports a battery level as its own: set the **Level** and the **Range** it is
counted in (1 to 100 unless you change it; the lowest value is shown as 0% and
the highest as 100%, and Android's kernel ignores a level of 0, so start the
range at 1). Android apps read it as the input device's battery
(`InputDevice.getBatteryState()`); the status bar does not show it. The level
is sent when the HID is connected and again whenever you apply a new one.
Changing only the level does not register the HID again; turning the battery
on or off, or changing its range, does.

While connected, a device's HIDs can still be changed. A switch adds or
removes that HID on the phone at once; changed settings (a resolution, a
button count) wait for *Apply*, which registers the HID again with the new
values. Either works while stopped, paused, or in Live control, not while a
script is playing: pause or stop first. What Live control was holding is
released before a change, and input waits about 100 ms after a HID is added
while Android registers it. A device can be left with no HID at all; it stays
connected and receives nothing. A HID that the phone refuses stays off and the
reason goes to the Activity panel.

Each connected phone shows a status dot, the HIDs it has, and its report count
(and its ADB Bridge port, see *ADB tab*), and a phone that stops responding is
dropped without stopping playback for the others. A script row goes to every
phone that has its HID; a phone without it skips the row, and so does a phone
whose settings the row does not fit (a position outside its screen, more
fingers than it declares). The Activity panel says which rows which phone
skipped.

**Player tab**

- The *Script* box shows the loaded script. Click it (or press **Ctrl+F**) to
  open the script list: type to search (words match anywhere in the name,
  case-insensitively), move with **Up/Down**, open with **Enter** or a click,
  close with **Esc**. Each row shows the file size and when it was last
  changed. The trash button deletes a file after an inline confirmation.
  Files outside `csv/` open by path from the bottom of the list, or by
  dropping them on the window. The list rescans whenever the window gets
  focus. The refresh button on the card rescans it too and, unless something
  is playing, reads the loaded script again, so a file edited in another
  program can be tried without choosing it anew; playback goes back to the
  start, and a file with a mistake leaves the last good one loaded.
- Play/pause (also **Space**), stop (**Esc** or **Ctrl+S**), and back-to-start
  (**Home** or **Backspace**) buttons. Each key can be remapped with the
  *Set...* buttons in the folded *Keys* row under the transport; a remapped
  action answers to that one
  key only, and *Reset* restores the defaults. The bar shows the lap being
  played: rows that run every lap in green, the lap blocks that run on this
  lap in purple. Click or drag it to seek within the lap, while playing,
  paused, or stopped. The label reads *Lap k of N*, or *Once* for a script
  that ends after its first lap.
- A script with problems shows every one of them (six lines here, all of them
  in `aoa_touch`).
- *Speed* is typed in directly (0.01×–1000×, with 0.5× / 1× / 2× / 4×
  buttons) and *Loops* counts laps, the first included (0 repeats until
  stopped); both apply immediately, even in the middle of a wait. *Lap* jumps
  to the start of any lap, with everything held as it would be after playing
  up to it. *Offset* shifts every later event by ±1 or ±10 ms;
  it starts at zero on every run and can only be changed while playing.
- Pause and stop release everything the script was holding. A seek rebuilds
  exactly what would be held at the new position (fingers down, keys,
  buttons, axes), so playback continues as if it had never jumped.

**Live tab**

The phone's shape, black, on the window's background, for using the phone
from the computer and watching what the scripts do. Live control starts as
soon as the tab is open while a phone is connected. With several phones, the
preview has the shape of the first one that has a touchscreen, and what is
sent goes to all of them: touches are scaled to each phone's own resolution,
and the Live finger is the last contact of the touchscreen that declares the
fewest. In the panel on the right, the *Forward* card's switches choose
what the preview forwards: *Touch* (drag inside the preview), *Mouse* (motion,
buttons, and the wheel), *Keyboard*, *Gamepad* (any controller GLFW
recognises), and *Toggle* (media and system keys); *Touch* and *Keyboard*
start on. Only profiles the connection actually has can be turned on, and
touch and mouse share the pointer, so only one of them is on at a time. Under
the switches are *Paste Text*, *Rotate*, the full screen button, and the
frame-rate limit of this window. The *Input* panel below them lists what is
held and what was sent, including an *Active touches* line per finger
currently down — Live's own included, called out from the rest — since a
script can be playing at the same time (see below); the same list also
appears on the Player tab. The Live finger itself appears on the preview with
its coordinates, held keys along its bottom.

The media keys sit under the preview: *Prev*, *Play/Pause*, and *Next*, with
*All keys* opening every Toggle key in groups (including *Stop*, *Home*,
*Back*, *Pan*, and *New*). To the right of the phone are its side keys, like
the buttons on a phone's frame: *Volume* + and − with *Mute*, and
*Brightness* + and −. These are Toggle keys, so they need the Toggle profile
and the *Toggle* switch on. Volume and brightness stay down for as long as
their button is held, like the phone's own keys; every other key sends one
tap per press.

*Rotate* turns the preview a quarter turn at a time for landscape use, and
the *Shape* row (folded away in the right panel) sets its shape as
width:height; both default to the connected touchscreen, and *Reset* returns
to it. Neither changes where a touch lands: the pointer's position inside the
preview is taken as a fraction, turned back into the phone's own orientation,
and only then scaled to the connected resolution. The expand button switches
the preview to a real, OS-level full screen window (not just this window's
own layout filling its still-windowed frame) with just the preview and the
input switches (including the Toggle keys), reached by right-clicking the
preview — the only way in, so nothing pops up on its own; Mouse mode's own
right button is not taken until the pointer is actually captured, so the menu
still works right up to that point. *Exit full screen* is in that menu. Full
screen keeps 5 px around the phone and draws its edge on all four sides, so a
screen whose ratio matches almost exactly still shows where the phone ends.
There is no keyboard shortcut for full screen any more, so it never conflicts
with a key a script or the target app needs. The preview shows why Live
control is unavailable right now (no device connected) under itself, in both
the normal and the full-screen view, instead of just a disabled toggle. Live
control now keeps running while a script plays, instead of turning off when
playback starts: its touch contact is always the last one the connection
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
*Mouse release key* (a folded row in the right panel) lets a different key take over
instead — useful when Esc itself needs to reach the phone (a game that quits
on Esc, for instance). Once changed, only that exact key releases the
capture; every other key, Esc included, goes straight to the phone like
normal. While captured, the release key is also spelled out directly on the
preview itself ("Press Esc to release the pointer", or whichever key was
chosen), including in full screen, so it stays visible without looking away
from the phone.

Keyboard forwarding recognises the extra keys a JIS (Japanese) keyboard has
that a US layout does not — Henkan, Muhenkan, Katakana/Hiragana,
Zenkaku/Hankaku, Ro, and Yen —
on Linux, by their physical scancode rather than by name: GLFW has no
`GLFW_KEY_*` constant for any of them on any platform, so without this they
would never reach the phone at all. This was a gap in this app talking to
GLFW, not a limitation of the phone or Android — once a usage reaches the
connection it is forwarded like any other key. The connection's *Usages*
range (in the Keyboard profile settings) has to include 0x87-0x94 for these
to actually reach the phone; raise its upper end from the default 0x65 if you
need them. ISO keyboards (the extra key beside left Shift, common outside
the US) are already covered by the default range on every platform. Windows
uses a different scancode numbering than Linux, so this recognition is
Linux-only for now; on Windows these keys still do not reach the phone.

A *reference image* (a screenshot works well) can sit over the preview:
choose it on the *Settings* tab, or drop the file on the preview while this
tab is open. While unlocked, drag inside it to move, its corner handle to
resize, and its top handle to rotate; *Lock image* (on the Settings tab, and
in the right panel while an image is loaded) freezes it in place and lets touches and clicks pass straight through to the
phone. Its path, position, size, rotation, opacity, and lock are saved
between runs, and the image is loaded again at the next start while the file
is still there.

**Playlist mode** (the Player tab)

The *Script | Playlist* switch at the top of the Player tab chooses what it
plays. *Playlist* plays several scripts in order, each for its own number of
loops, with an optional limit in minutes on the whole run. *Loops* under the
list is how many times the whole list plays (0 repeats it until stopped, as
for a single script), and the bar then shows which loop it is on. It uses the same
play bar as a single script: the bar shows the script that is running (for
example *2/5*), and play/pause, stop, speed, offset, and seeking act on that
script. *Loops* is set for each script in the list instead. Playlists are
saved as `playlists/<name>.playlist` next to the program. The *Playlist* box
shows the open one; click it for the list of saved playlists, which works
like the script list (type to search, **Up/Down**, **Enter**, **Esc**, and a
trash button with an inline confirmation). With room, the list sits on the
left and the play bar on the right; in a narrow window they stack. *Add a
script* under the list opens the same picker as Script mode (type to search,
**Up/Down**, **Enter** adds the script); `.csv` files dropped on the Playlist
join the end of the list. The switch is locked
while something plays; **Ctrl+F** and *Open in player* switch back to
*Script*.

**Recorder tab**

Choose the phone (automatic works when adb sees only one; turning on a
phone's bridge in the *ADB tab* chooses its `127.0.0.1:<port>`) and optionally limit
it to one `/dev/input/eventN`. Type the *File name* before you start (`.csv` is
added; blank gives `record-<date>-<time>`). Names with characters Windows or
Linux cannot store are refused, and a name that already exists turns the
button into *Replace and record*. *Stop and save* writes the file into `csv/`,
and *Open in player* loads it. The existing file is replaced only when the new
recording stops with something captured; until then the rows are kept in
`<name>.csv.part` beside it.

*Coordinates* chooses how touch positions are written: **Raw** (the panel's own
numbers) or **Normalized** (fractions from 0 to 1). The line under the switch
shows what a tap in the middle of the screen looks like in the chosen mode. Where a touch lands is the same in
both. The choice is remembered and is fixed while recording; see
[`--coords`](CLI.md#aoa_record--usage).

**ADB tab**

While this app holds a phone over USB, adb cannot open it on its own — on
Windows never, because a USB device can be opened by only one program. The
*ADB* tab keeps adb working anyway: each connected phone has its own line
with a bridge switch and a *Port*. Turning a bridge on serves that phone's adb interface on
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
closes the bridge before the phone is closed and runs `adb disconnect`, then
stops the adb server (unless a recording is running). adb's libusb backend,
the default on Linux and macOS, tries to claim a phone's adb interface only
when the phone first appears; if the bridge held it then, the server never
retries, so the next adb command has to start a fresh server. See
[aoahid_adb_proxy's USAGE.md](https://github.com/nemarpuc/aoahid_adb_proxy/blob/main/docs/USAGE.md)
for the adb source references.
Bridges cannot be switched while a script plays, since stopping one can take
up to a second.

The bridge has been verified end to end on real hardware with a Samsung
Galaxy Tab S11 and a POCO F6 Pro (HyperOS), on both Windows 10 x64 and Arch
Linux, with the phone's driver on WinUSB and on libusbK. On Windows the HyperOS
phone worked as plugged in, while the Samsung tablet first needed its driver
switched to WinUSB. If a bridge does not start on Windows, see
[Troubleshooting](TROUBLESHOOTING.md).

**Settings tab**

How the window looks, all saved between runs:

- *Presets*: save the whole look (everything below) under a name, then
  pick it from the list and *Apply* it (or double-click it), or *Delete* it.
  Presets are `themes/<name>.theme` next to the program, in the same
  `key = value` form as the settings file.
- *Appearance*: the *Dark* or *Light* theme; the *Accent* colour (eight
  swatches or any colour; the light theme uses a deeper shade of it so white
  text on it stays readable); the *Size* of everything (Small, Normal,
  Large, Larger, on top of the display's own scale); and *Motion*, a short
  fade when the tab changes.
- *Glass*: *Glass look* turns the cards, popups, and keys translucent over the
  background (with glass off, cards and keys are solid colours with high
  contrast). With glass on: *Clear*, how much the cards let the background
  show through (0% is solid, up to 90%); *Gloss* and *Rim*, the light on their
  top and edges (0-200%). *Corners* sets the corner radius (0-24 px) in both
  modes.
- *Background*: *None* (the theme's colour), a *Color*, or an *Image*
  (.png/.jpg/.bmp: *Choose...* opens the system's file picker, or type a
  path and press Enter, or drop it on the window on any tab but Live). The
  background also shows around the phone on the Live tab. An image covers
  the whole window, full screen Live included; with glass on, the cards show
  a blurred copy of what is behind them (*Blur*, in window pixels; 0 shows
  the picture sharp, as clear glass), and *Dim* darkens it (lightens it in the
  light theme) so text stays readable. The image is loaded again at the next
  start while the file is still there.
- *Live preview*: *Glass phone screen* (when Glass look is on) makes the phone's
  screen in the Live preview glass like the cards instead of black, with its own
  *Clear* (0% solid to 100% clear); *Corners* rounds the phone's corners (0-60 px),
  glass or black. Around the phone, the window's background always shows.
  Also the reference image described under *Live tab*.
- *Layout*: *Show activity panel*, and *Notifications*, which show warnings,
  errors, connecting, and disconnecting for a few seconds in the bottom-right
  corner (hover keeps one up, a click dismisses it). Startup messages go
  only to the Activity panel, unless it is hidden.
- *Control API*: the switch and the port of the local HTTP server described
  in [CONTROL_API.md](CONTROL_API.md).

On a Wayland session with XWayland available, the GUI opens through
XWayland, because GLFW's native Wayland backend has no input method support;
that is what lets the script search (and every other text field) take
Japanese and other IME input via fcitx5 or ibus. When `XMODIFIERS` is unset it
is set to `@im=ibus` if `GTK_IM_MODULE` is `ibus`, and to `@im=fcitx`
otherwise.

The window only redraws when something changes, so an idle window uses no
CPU. It follows the monitor's scale factor on Windows and X11 and the
compositor's scale on Wayland.
