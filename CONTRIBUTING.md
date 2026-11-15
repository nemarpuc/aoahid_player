# Contributing

Use the [issue tracker](https://github.com/nemarpuc/aoahid_player/issues) for
proposed changes and submit patches through
[pull requests](https://github.com/nemarpuc/aoahid_player/pulls).

## Where things are

- `src/`, `include/`: `aoahid_player_core`, the engine with no console I/O. It reports through `EventSink`.
- `apps/`: the three programs (`aoahid_player_gui`, `aoa_touch`, `aoa_record`).
- `tests/`: unit tests (doctest, vendored under `tests/third_party/`).
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): the core, the GUI's threads, how time is kept.
- [docs/CROSS_REPO.md](docs/CROSS_REPO.md): what crosses the boundary to libaoahid and
  aoahid_adb_proxy, the version pins, and the release order.

All HID, AOA, and USB behavior belongs to [libaoahid](https://github.com/nemarpuc/libaoahid).
A change that needs a new protocol detail goes there first.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/libaoahid
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```

On Linux, `make AOAHID_PREFIX=/path/to/libaoahid` and `make test` do the same.
The README's [Building from source](README.md#building-from-source) lists the
requirements (the GUI needs network access at configure time for its pinned dependencies,
or `-DAOAHID_PLAYER_BUILD_GUI=OFF`). The tests need no phone.

## Style

No formatter is configured: follow the code around your change. Keep the engine free of
console I/O and of anything that blocks the playback thread.

## Releasing

Push a tag `vX.Y.Z`: the release workflow builds an archive per platform and CPU and
publishes them. Run the workflow by hand with `dry_run` set to `true` to build and package
everything without publishing. The libaoahid release the binaries link against is pinned in
the workflows; see [docs/CROSS_REPO.md](docs/CROSS_REPO.md).

## Submitting changes

- Add a unit test for a changed CSV rule, key name, coordinate rule, or input-state transition.
- Put a user-visible change in `CHANGELOG.md`.
- Say in the pull request what was run. A claim that something works on a phone needs the
  phone, the host OS, and the profile used; the pen and digitizer profiles are not yet tested
  on hardware.

## Code of Conduct

Everyone taking part is expected to follow the [Code of Conduct](CODE_OF_CONDUCT.md).
