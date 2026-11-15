# Building from source

## Requirements

- CMake 3.21+ and a C++20 compiler (GCC or Clang on Linux, MSVC on Windows)
- [libaoahid](https://github.com/nemarpuc/libaoahid) 0.2.x
  (see below)
- For the GUI on Linux, the X11 and Wayland development headers GLFW builds
  against, and D-Bus for the file picker, for example on Debian/Ubuntu:
  `libwayland-dev libxkbcommon-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev libdbus-1-dev pkg-config`
  (Arch/CachyOS: `wayland libxkbcommon libx11 libxrandr libxinerama libxcursor libxi libxext dbus`).
  At run time GLFW loads X11 or Wayland and OpenGL itself, so the GUI runs
  on either. The file picker (*Choose...* on the Settings tab) is the
  desktop's own, opened through xdg-desktop-portal; without a portal it
  logs why and the path field still works.
- Network access at configure time for the GUI: Dear ImGui 1.92.9, GLFW
  3.5.1, cpp-httplib 0.57.1 (the control API's HTTP server; see
  [Control API](CONTROL_API.md#control-api)), and nativefiledialog-extended 1.4.1 (the
  file picker) are downloaded at pinned, SHA-256-checked releases; ImGui,
  GLFW, and nativefiledialog-extended are linked statically, cpp-httplib is
  header-only. To build offline, pass
  `-DFETCHCONTENT_SOURCE_DIR_IMGUI=<dir> -DFETCHCONTENT_SOURCE_DIR_GLFW=<dir> -DFETCHCONTENT_SOURCE_DIR_HTTPLIB=<dir> -DFETCHCONTENT_SOURCE_DIR_NFD=<dir>`
  pointing at local copies of those releases, or `-DAOAHID_PLAYER_BUILD_GUI=OFF`.

## Build

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

## Getting libaoahid

`find_package(aoahid CONFIG REQUIRED)` fails immediately if CMake cannot
find libaoahid. It is not packaged by distributions or vcpkg/conan, so get it
one of these two ways.

**Option A — prebuilt release (fastest, no libusb build needed):** download
the shared-library archive for your platform from
[libaoahid's releases](https://github.com/nemarpuc/libaoahid/releases)
(the same asset the workflows here use), extract it, and point
`CMAKE_PREFIX_PATH` at the extracted directory:

```sh
curl -LO https://github.com/nemarpuc/libaoahid/releases/latest/download/libaoahid-<version>-linux-x86_64-ubuntu22.04.tar.gz
tar -xzf libaoahid-<version>-linux-x86_64-ubuntu22.04.tar.gz
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/libaoahid-<version>-linux-x86_64-ubuntu22.04"
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

## Testing

```sh
ctest --test-dir build --output-on-failure -C Release
```

Unit tests cover CSV parsing (rows, directives, key names, error reports), the
script laps and coordinate scaling, how recordings write
coordinates, `adb shell getevent` parsing, HID descriptor bit widths, and the
input-state transitions used for stop, pause, and seek, including the state
on any lap. They use [doctest](https://github.com/doctest/doctest),
vendored under `tests/third_party/`. They are built only when this is the
top-level project; pass `-DBUILD_TESTING=OFF` to skip them.
