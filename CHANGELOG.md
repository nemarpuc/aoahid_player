# Changelog

All notable changes to aoahid_player are recorded here. Entries before 0.20.2
were reconstructed from the commit history.

## [Unreleased]

- GUI: the Speed and Loops fields (and every other text field) take typed
  input while Live control forwards keys. Before, the forwarding cleared all
  keyboard input every frame, so nothing could be typed.
- GUI: in Live mouse mode, only a click on the preview captures the pointer.
  Before, a mouse button a script was holding made a click anywhere in the
  window capture it.
- README: a place for the demo video.
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
