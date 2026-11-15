# Troubleshooting and known limitations

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| Windows: the ADB Bridge does not start, and the error contains `libusb status -12` | The phone's adb interface uses a driver that libusb cannot use. Some manufacturers install their own USB driver by default. | Switching the whole phone to WinUSB with Zadig may fix it ([example below](#example-samsung-on-windows)); it is not guaranteed on every phone. |

### Example: Samsung on Windows

A Samsung tablet on Windows, with Samsung's USB driver installed as it is by
default, connected and played scripts normally, but its ADB Bridge would not
start. The *ADB* tab showed:

```text
Could not start: the phone's ADB interface could not be opened:
AOAHID_ERR_UNSUPPORTED (channel.open: The Bulk interface could not be read,
claimed, or started.), libusb status -12. USB debugging may be off, or another
program holds the interface.
```

When the interface needs a driver libusb can use, the message ends with "The phone's adb
interface needs a driver libusb can use, such as WinUSB" instead of the USB debugging hint.

`-12` is libusb's `LIBUSB_ERROR_NOT_SUPPORTED`. On Windows, libusb can only
claim an interface whose driver is WinUSB, libusbK or libusb0. HID still
worked because it goes through the phone's control endpoint, not the adb
interface. USB debugging and the adb server had nothing to do with it.

Why Samsung and not the HyperOS phone: on Windows the HyperOS phone's adb
interface came up with WinUSB from the start, so the bridge worked with no
changes. Samsung ships its own dedicated USB driver, and Windows uses it for
Samsung devices instead of WinUSB. On the tablet, the parent device "SAMSUNG
Mobile USB Composite Device" used Samsung's `dg_ssudbus` (version 2.21.4.0).
libusb accepts `dg_ssudbus` as a composite parent, but the adb interface under
it did not get WinUSB, so libusb could not claim it.

The fix was to remove Samsung's driver and give the phone WinUSB:

1. Plug in the phone and open Device Manager.
2. Under *Universal Serial Bus controllers*, right-click the phone's parent
   device ("SAMSUNG Mobile USB Composite Device" for Samsung) and choose
   *Uninstall device*. Tick *Attempt to remove the driver for this device*
   (*Delete the driver software for this device* on Windows 10) and click
   *Uninstall*. Without this tick, Windows puts the same driver back.
3. Unplug the phone and plug it back in.
4. Download and run [Zadig](https://zadig.akeo.ie/).
5. Turn on *Options → List All Devices*.
6. Pick the entry for the whole phone, not one of its interfaces. On the
   Samsung tablet it was "SAMSUNG Android"; the entries for its adb and MTP
   interfaces (names ending in "(Interface N)") did not fix it when changed.
   The names depend on the device; the whole phone's *USB ID* has two boxes
   (vendor and product), while an interface entry has a third one.
7. Set the driver on the right of the arrow to *WinUSB* and click
   *Replace Driver* (*Install Driver* if it had none).
8. Connect the phone in this program again and turn its bridge on.

With the whole phone on WinUSB, libusb treats it as one WinUSB device and
reaches every interface, adb included, through it. Replacing only the adb
interface's driver left the bridge failing on the Samsung tablet, most
likely because Samsung's composite driver does not expose that interface in
a way libusb can map; this was not examined further.

Other manufacturers that ship their own dedicated USB driver can be handled
the same way. A phone whose adb interface is already WinUSB, like the HyperOS
phone, needs none of this.

After the change, Windows `adb` still sees the phone over USB as before.
Tools that need the manufacturer's driver (such as Samsung Smart Switch) may
stop working with it, and Windows file transfer (MTP) may too; MTP was not
checked. To undo, repeat steps 1-3, or reinstall the manufacturer's USB driver.

## Known limitations

- Lap blocks do not nest, and a block's laps are fixed numbers: there is no
  random or conditional choice.
- Recording captures touches and keyboard keys only, not mouse, gamepad,
  pen, or digitizer input.
- Recording tracks the multi-touch Type B protocol (`ABS_MT_SLOT` plus
  `ABS_MT_TRACKING_ID`); the older Type A `SYN_MT_REPORT` form is not parsed.
- libaoahid's battery and raw profiles are not exposed.
- A phone that reports no serial is remembered by its USB address, so its
  settings do not follow it to another port; phones that report the same
  serial share their settings.
- HIDs cannot be added or removed while a script is playing, only while it is
  paused or stopped.
