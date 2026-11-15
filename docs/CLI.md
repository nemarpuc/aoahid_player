# Command-line programs

## aoa_touch — usage

```
aoa_touch [options] [script.csv]

Device selection:
  --devices <list>        Comma-separated device numbers from the discovery
                           list (e.g. "1,3"), or "all". If omitted, an
                           interactive numbered list is shown.

Profile setup (all explicit; there are no presets):
  --touch-res WxH          Touch surface resolution, e.g. 1080x1920
  --touch-max-contacts N   Max simultaneous touch contacts (1-16, default 16)
  --gamepad-buttons N      Number of gamepad buttons
  --gamepad-axes LIST      Comma-separated axis roles, e.g. x,y,rx,ry
                           (x y z rx ry rz slider dial wheel rudder
                            throttle accelerator brake steering;
                            x and y are required)
  --gamepad-axis-bits N    Bit width per axis, 2-32
  --gamepad-dpad MODE      none|hat|buttons
  --key-usage-range LO,HI  Keyboard HID usage range (default 0x04,0x65)
  --mouse-buttons N        Number of mouse buttons
  --pen                    Stylus on the Pen collection (needed by p rows)
  --digitizer              Stylus on the Digitizer collection (needed by d rows)
  --toggle                 Media keys (Consumer Control; needed by c rows)

Playback:
  -A                       Auto-detect touch resolution via `adb shell wm size`
  --speed FACTOR           Playback speed multiplier (default 1.0)
  --loop N                 Stop after N laps, the first included (default: infinite)
  --no-prompt              Do not start the live "-> " offset prompt

ADB Bridge (keeps adb usable while this program holds the phone):
  --adb-port N             First port on 127.0.0.1 (default 6555; the next
                           device gets the next port)
  --no-adb-bridge          Do not start it (the adb server is left alone)
  -h, --help               Show this text
```

The ADB Bridge is the GUI's (see *ADB tab*), but with no tab to switch it,
`aoa_touch` starts it for every device unless `--no-adb-bridge` is given:
the adb server is stopped before the devices are opened, each device's
bridge starts after its HID devices are registered, and
`adb connect 127.0.0.1:<port>` runs in the background while playback starts.
Record from another terminal with `aoa_record -s 127.0.0.1:6555`.

With no script path, the `csv/` folder next to the executable is listed for
interactive selection. A script given on the command line is checked before
any USB work, so a mistake is reported with its file and line straight away.

While playing, type a number of milliseconds at the `->` prompt to shift every
later event (negative is earlier). Ctrl+C stops playback and releases
everything that was held before the devices are closed.

## aoa_record — usage

```
aoa_record [options]

  -o, --output NAME   Recording name or path. A bare name is saved in the
                       csv/ folder next to this program (".csv" is added
                       when missing). Default: record-<date>-<time>
  -s, --serial ID     adb device serial, for `adb -s ID`. A host:port
                       serial, such as the ADB Bridge's 127.0.0.1:6555,
                       is `adb connect`ed first
      --input PATH    Only record /dev/input/eventN; default is every
                       device getevent reports
      --coords MODE   How touch coordinates are written (default: raw)
                       raw         the touch panel's own values, with
                                   "@screen WxH" naming its range
                       normalized  fractions 0..1 of the panel, written with
                                   "@coords normalized"
      --echo          Print each captured row while recording
  -h, --help          Show this text
```

Runs `adb shell getevent -lt`, turns touch (`ABS_MT_*`) and key (`EV_KEY`)
frames into lowercase `t` and `k` rows with measured `wait_ms`, and streams
them to `<name>.part`, which becomes the file when the recording stops. The
touch panel's own coordinate range is read with
`getevent -lp`, so playback can scale the recording to whatever touchscreen
resolution is connected:

- `raw` (the default) writes the panel's own values and names its range with
  `@screen WxH`.
- `normalized` writes fractions of the panel (`0.500000` is the middle) under
  `@format 3` and `@coords normalized`.

If the panel's range cannot be read, the recording is written raw and a
warning says so. Ctrl+C stops; the file is flushed and closed first. A
recording that captured nothing, or could not start, leaves no file behind and
does not touch a file already saved under that name.
