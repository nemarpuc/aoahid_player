# Using `aoahid_player_core` as a library

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

- `session.hpp` — device discovery, connect and disconnect with one
  `ProfileSetup` per device and optional `AdbBridgeOptions` (the ADB Bridge,
  owned by each `Device` in `device.hpp`), and `reconfigure()` to add or
  remove a connected device's HIDs. `DeviceGroup::apply()` in
  `device_group.hpp` takes the `Space` a row's positions are in.
- `player.hpp` — playback with pause, seek, speed, loop limit, live offset,
  and a wall-clock stop time, all callable from any thread; `status()` is
  lock-free, and a `PlaybackObserver` sees every row sent. `set_live_pump()`
  lets another input source (the GUI's Live tab) interleave into the same
  `DeviceGroup` while a script plays; `wake_live()` wakes it promptly.
- `recorder.hpp` — `adb getevent` recording into a CSV file, with the raw and
  normalized coordinate modes.
- `event_script.hpp` — CSV loading (rows in file order, directives, key names)
  and the script laps.
- `adb.hpp`, `paths.hpp`, `events.hpp` — adb helpers, UTF-8 paths and the
  `csv/` folder, and the `EventSink` interface.

`CMAKE_PREFIX_PATH` needs both this package's prefix and libaoahid's.
