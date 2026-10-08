# The three repositories

aoahid_player is the top of a stack of three repositories. This page records
what crosses each boundary, which values have to be kept in step by hand, and
the order a change travels in. It lives here because this repository is the
only one that depends on both of the others.

Source references are `repo/path#Lnnn`, valid at libaoahid v4.2.0,
aoahid_adb_proxy v3.1.3, and aoahid_player v1.1.3.

## Stack

```text
libaoahid          C++20 library, stable C ABI (aoahid.h): AOA 2.0 HID over libusb
  └─ aoahid_adb_proxy     C++11: serves the phone's ADB bulk interface on a local TCP port
       └─ aoahid_player   C++20: scripted playback, recording, live control (GUI and CLIs)
```

| Repository | Owns | Does not own |
| --- | --- | --- |
| libaoahid | USB access, AOA requests, HID descriptors, report state machines, the bulk Channel | Any product value: usages, ranges, widths, and target limits are caller inputs. |
| aoahid_adb_proxy | ADB packet framing between TCP and the Channel | Device lifetime. It borrows the caller's `aoahid_device`. |
| aoahid_player | Profile settings, the CSV format, timing, the adb tooling around a connection | USB or HID details. It calls the C API and the proxy's two functions. |

## How a script row becomes a USB transfer

| Step | Where | Data |
| --- | --- | --- |
| Parse | `aoahid_player/src/event_script.cpp#L123` | CSV text to an `EventPayload` variant plus `wait_ns`. |
| Schedule | `aoahid_player/src/player.cpp#L263` | The row's absolute deadline on the playback thread. |
| Apply | `aoahid_player/src/device_group.cpp#L35` | One `aoahid::` call per device, for example `touch(node, id, state, x, y, nullptr)`. |
| State | libaoahid `src/profiles/state.cpp` | The Node's state changes; nothing is sent. |
| Submit | `aoahid_player/src/device_group.cpp#L233` | `aoahid_node_submit`, then `aoahid_node_submit_blocking` for each dirty Node. |
| Serialize | `libaoahid/src/profiles/state.cpp#L318` | The input report, straight into a transfer pool slot. |
| Transfer | `libaoahid/src/transport/transport.cpp#L286` | AOA request 57 on endpoint 0. |

The ADB path is separate: adb's TCP connection to
`aoahid_adb_proxy/src/aoahid_adb_proxy.cpp#L140` and
`aoahid_adb_proxy/src/aoahid_adb_proxy.cpp#L175`, then
`aoahid_channel_write` and `aoahid_channel_read` on the same USB handle.

## What each boundary uses

aoahid_player calls these libaoahid functions: `aoahid_context_create`,
`aoahid_context_destroy_blocking`, `aoahid_discover`,
`aoahid_discovery_count`, `aoahid_discovery_get`, `aoahid_discovery_destroy`,
`aoahid_accessory_start`, `aoahid_device_open`, `aoahid_device_close`,
`aoahid_spec_create_touchscreen`, `_mouse`, `_keyboard`, `_gamepad`, `_pen`,
`_toggle`, `aoahid_spec_release`, `aoahid_node_open`, `aoahid_node_close`,
`aoahid_node_submit`, `aoahid_node_submit_blocking`, `aoahid_last_error`,
`aoahid_result_name`, and the profile calls through the `aoahid::`
forwarders in `aoahid.hpp`.

aoahid_adb_proxy calls four: `aoahid_channel_open`, `aoahid_channel_read`,
`aoahid_channel_write`, `aoahid_channel_close`.

aoahid_player calls two proxy functions: `aoahid_adb_proxy_start` and
`aoahid_adb_proxy_stop`.

## Assumptions that cross a boundary

Each of these is true today and is not enforced by a compiler. A change on
one side has to be made on the other.

| Assumption | Relied on in | Provided by |
| --- | --- | --- |
| Every call on one Context comes from one thread at a time. | The whole core; the GUI funnels calls through one worker (`aoahid_player/apps/aoahid_player_gui/engine.cpp#L370`). | libaoahid's header contract. |
| The Context is in internal-thread mode. | The proxy reads and writes the Channel from two threads. | `aoahid_player/src/context.cpp#L100` |
| The proxy is stopped before the Device is closed. | The proxy's Channel must not be closed while its threads run. | `aoahid_player/src/device.cpp#L244` |
| Input starts no sooner than about 100 ms after `aoahid_node_open`. | Android registers the HID device asynchronously; an earlier report is refused. | `aoahid_player/src/session.cpp#L18` |
| `aoahid_device_close` consumes the Device and its Nodes even when it returns `AOAHID_CLOSE_PENDING`. | The player drops its pointers after one call. | libaoahid's header contract for `aoahid_device_close`. |
| After `AOAHID_ADB_PROXY_ERR_INTERFACE`, the calling thread's `aoahid_last_error()` is the failed `aoahid_channel_open`. | `aoahid_player/src/device.cpp#L138` reads `libusb_status` to tell a driver problem (-12, `LIBUSB_ERROR_NOT_SUPPORTED`) from an adb server holding the interface. | The proxy makes no libaoahid call after a failed open. |
| A context destroy that timed out and kept ownership reports the field name `context.destroy`. | `aoahid_player/src/context.cpp#L116` compares that string to decide whether to retry. | `libaoahid/src/api/c_api.cpp#L2237`. Renaming the string in libaoahid breaks the retry without a build error. |
| The Linux HID parser limits the player passes are the ones in libaoahid's `docs/LIMITS.md`. | `aoahid_player/src/device.cpp#L42` | libaoahid has no default for them. |
| The media-key Usages, their semantics, and their expected Linux codes are parallel arrays in one order. | `aoahid_player/include/aoahid_player/spec_builder.hpp#L431` and the CSV parser share `media_usages`. | A `static_assert` checks the lengths, not the order. |
| An ADB packet is at most 1 MiB and its header is 24 bytes. | The proxy's buffers and Channel options. | AOSP `adb.h` (`MAX_PAYLOAD`). |

## Copies that must match

| Copy | Original | Check |
| --- | --- | --- |
| `aoahid_player/third_party/aoahid_adb_proxy/` | `aoahid_adb_proxy/src/aoahid_adb_proxy.cpp` and `include/aoahid_adb_proxy.h` | `diff -r`. The version and commit are named in `aoahid_player/src/CMakeLists.txt#L20`. |
| `aoahid_player/udev/51-aoahid.rules` | `libaoahid/udev/51-aoahid.rules` | The rules are the same; the comments differ. |
| Python, C#, and Rust struct definitions under `libaoahid/bindings/` | `libaoahid/include/aoahid.h` | `tools/check_bindings.py` and `tests/abi` in libaoahid's CI. |

## Version pins

| Pin | Where | Meaning |
| --- | --- | --- |
| libaoahid minimum for the player | `aoahid_player/CMakeLists.txt#L37` | The oldest libaoahid the source builds and runs against. The comment above it explains each raise. |
| libaoahid release the player's binaries link | `aoahid_player/.github/workflows/ci.yml#L20` and `aoahid_player/.github/workflows/release.yml#L31` | A tag whose release assets CI downloads. It must already be published. |
| libaoahid minimum for the proxy | `aoahid_adb_proxy/CMakeLists.txt#L16` | 4.0. |
| libaoahid release the proxy's CI builds against | `aoahid_adb_proxy/.github/workflows/ci.yml#L13` | The oldest supported release, on purpose. |
| libaoahid source fallback for the proxy | `aoahid_adb_proxy/CMakeLists.txt#L22` | A commit, used only when no installed package is found. |
| libusb | libaoahid requires 1.0.30 at build time and at run time (`libaoahid/src/transport/libusb_include.hpp#L35`) | The player's release archives ship a matching libusb next to libaoahid. |

## Release order

A change travels down the stack. Each step must be finished, tag included,
before the next one starts, because the next one's CI downloads the previous
one's release.

1. **libaoahid.** Write the version into every site, add the changelog
   section, push, wait for CI on that commit, then tag. The tag starts the
   release workflow, which publishes the assets.

   ```sh
   python3 tools/release/sync_version.py X.Y.Z
   # CHANGELOG.md: add "## [X.Y.Z] - YYYY-MM-DD" with the hardware-verification status
   cmake --preset dev && cmake --build --preset dev && ctest --preset dev
   git status --short            # list anything untracked or unintended
   git add <each file by name>
   git commit -m "Release X.Y.Z: <summary>"
   git push origin main
   gh run watch --exit-status "$(gh run list --commit "$(git rev-parse HEAD)" --workflow CI --json databaseId --jq '.[0].databaseId')"
   git tag vX.Y.Z && git push origin vX.Y.Z
   gh release view vX.Y.Z        # the assets the other two download
   ```

2. **aoahid_adb_proxy**, only if it changed. The version is in
   `CMakeLists.txt` and the three macros in `include/aoahid_adb_proxy.h`; the
   changelog needs a `## X.Y.Z` section, which becomes the release notes.

   ```sh
   cmake -S . -B build -DCMAKE_PREFIX_PATH=<libaoahid prefix> && cmake --build build && ctest --test-dir build
   git status --short
   git add <each file by name>
   git commit -m "Release X.Y.Z: <summary>"
   git push origin main          # wait for CI as above
   git tag vX.Y.Z && git push origin vX.Y.Z
   ```

3. **aoahid_player.** Copy the proxy's two files over the vendored ones and
   update the comment that names their version and commit. Set `AOAHID_TAG`
   in both workflows to the libaoahid release. Raise the version in
   `CMakeLists.txt`, add the changelog section, and update the proxy version
   in the README's license list.

   ```sh
   cp ../aoahid_adb_proxy/src/aoahid_adb_proxy.cpp ../aoahid_adb_proxy/include/aoahid_adb_proxy.h third_party/aoahid_adb_proxy/
   make AOAHID_PREFIX=<libaoahid prefix> && make test
   git status --short
   git add <each file by name>
   git commit -m "Release X.Y.Z: <summary>"
   git push origin main          # wait for CI as above
   git tag vX.Y.Z && git push origin vX.Y.Z
   ```

A libaoahid release that changes no API needs no change to the player's
`find_package` minimum, only to `AOAHID_TAG`.

Which version number to raise:

| Change | libaoahid | proxy | player |
| --- | --- | --- | --- |
| Behavior fix, no API change | patch | patch | patch |
| New function, option, or CSV feature that old inputs do not see | minor | minor | minor |
| A struct layout, a function signature, a removed symbol, or a CSV change that alters how an existing file plays | major | major | major |

A libaoahid major release changes the ABI: the bindings, the layout oracle,
and the golden ABI data change with it, and both downstream repositories must
raise their minimum.
