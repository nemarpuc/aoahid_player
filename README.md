# aoahid-player

[![ci](https://github.com/nemarpuc/aoahid_player/actions/workflows/ci.yml/badge.svg)](https://github.com/nemarpuc/aoahid_player/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Plays scripted touch, mouse, keyboard, gamepad, pen, and digitizer input to Android
devices over USB with AOA 2.0 HID, and records real touches and key presses
into the same script format with `adb`. Linux and Windows, x86_64 and ARM64,
C++20.



https://github.com/user-attachments/assets/87f32f5a-7501-4257-b0ac-dbac69ed97b7



There are three programs, all built on one shared engine:

- `aoahid_player_gui` — a desktop app for connecting, playing, seeking, and
  recording.
- `aoa_touch` — the command-line player.
- `aoa_record` — the command-line recorder.

All HID, AOA, and USB work is done by
[libaoahid](https://github.com/nemarpuc/libaoahid); see that repository for
the protocol, descriptor, and transport details.

Tested on real hardware: touchscreen, keyboard, mouse, gamepad, and media keys
(the *toggle* profile) on a Samsung Galaxy Tab S11 and a POCO F6 Pro, from
Windows 10 x64 and Arch Linux. The pen and digitizer profiles have not been
tested on hardware yet.

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
  docs/           the documentation the README links to
  third-party/    licences of everything bundled
  README.md  LICENSE
```

Keep the folder together: the programs find the libraries and `csv/` next to
themselves. On Linux, install the udev rule first (see
[Linux permissions](#linux-permissions-udev)). Recording needs `adb`
(Android SDK Platform-Tools) on PATH; playback does not. The Windows ARM64
archive is cross-compiled, so CI checks its contents but does not run it.

On Windows, if the phone cannot be opened or the ADB Bridge does not start, the
cause is often the phone's driver: libusb needs WinUSB (or libusbK / libusb0).
Input alone worked with Samsung's own driver on the Galaxy Tab S11, while the
ADB Bridge needed the whole device switched to WinUSB. Replacing the driver
with [Zadig](https://zadig.akeo.ie/) may fix it, but it is not guaranteed on
every phone; see [Troubleshooting](docs/TROUBLESHOOTING.md).

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

## Documentation

| Document | What it covers |
|---|---|
| [docs/GUI.md](docs/GUI.md) | The desktop app: devices, live control, playing, recording, the ADB tab, settings |
| [docs/CLI.md](docs/CLI.md) | `aoa_touch` (player) and `aoa_record` (recorder) |
| [docs/CSV_FORMAT.md](docs/CSV_FORMAT.md) | The script format: rows, laps, directives, coordinates, key names |
| [docs/CONTROL_API.md](docs/CONTROL_API.md) | The local HTTP API for driving the phone from another program |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Driver problems (Samsung on Windows) and known limitations |
| [docs/BUILDING.md](docs/BUILDING.md) | Building from source, getting libaoahid, testing |
| [docs/LIBRARY.md](docs/LIBRARY.md) | Using `aoahid_player_core` in your own program |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | For contributors: the core, the GUI's threads, how time is kept |
| [docs/CROSS_REPO.md](docs/CROSS_REPO.md) | For contributors: the boundaries to libaoahid and aoahid_adb_proxy, version pins, release order |

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
- [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended)
  (zlib), linked into the GUI for the file picker
- [aoahid_adb_proxy](https://github.com/nemarpuc/aoahid_adb_proxy) 0.1.0
  (MIT), vendored under `third_party/aoahid_adb_proxy/` for the ADB Bridge
- the Roboto font (Apache-2.0), embedded in the GUI
- [stb_image](https://github.com/nothings/stb) (MIT/public domain), vendored
  for the background and reference images
- on Windows, the Microsoft Visual C++ runtime DLLs, redistributed under
  Microsoft's redistribution terms

The test suite vendors doctest (MIT, `tests/third_party/LICENSE-doctest.txt`);
it is not linked into any program.
