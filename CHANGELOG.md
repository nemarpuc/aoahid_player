# Changelog

All notable changes to aoahid_player are recorded here.

## [Unreleased]

- Builds against libaoahid 0.2.0 (`find_package(aoahid 0.2.0)`; a 0.x minor change is not accepted by 0.1.0). The CI and release pins name the 0.2.0 archives.

## [0.1.0] - 2026-11-09

The first release of this line. Before 1.0.0 a change of the minor number may change the CSV
format, the control API or the settings file. Earlier version numbers (0.x to 3.x) are gone; the
git history is kept.

- Builds against libaoahid 0.1.0 and the vendored aoahid_adb_proxy 0.1.0. The CI and release pins
  name libaoahid v0.1.0.
- CSV-scripted HID playback and recording (GUI and CLIs), per-device HIDs and settings, ADB Bridge,
  and the local HTTP control API on port 47821.
- **Not run against a phone** since the version line restarted; the unit tests and CI cover the core.
