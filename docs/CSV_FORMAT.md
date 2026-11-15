# CSV script format

Plain text, no header row, one event per line. `#` starts a comment; blank
lines and a UTF-8 byte-order mark are ignored. Lines starting with `@` are
[directives](#directives).

| prefix | profile        | columns                                     |
|--------|----------------|---------------------------------------------|
| `t`    | touch          | `finger_id,state,x,y,wait_ms`               |
| `m`    | mouse move     | `dx,dy,wait_ms`                             |
| `b`    | mouse button   | `button_no,pressed,wait_ms`                 |
| `k`    | keyboard key   | `usage_or_name,down,wait_ms` (a usage number, `0x..` accepted, or a [key name](#key-names)) |
| `g`    | gamepad button | `button_no,pressed,wait_ms`                 |
| `a`    | gamepad axis   | `axis_index,value,wait_ms`                  |
| `h`    | gamepad dpad   | `up,down,right,left,wait_ms`                |
| `p`    | pen            | `in_range,tip,x,y,pressure,wait_ms`         |
| `d`    | digitizer      | `in_range,tip,x,y,pressure,wait_ms`         |
| `c`    | media key (toggle) | `usage_or_name,down,wait_ms` (a Consumer usage number, or a [media key name](#media-key-names)) |
| `w`    | —              | `wait_ms` (only waits)                      |

`state`, `down`, `pressed`, `in_range`, `tip`, and the four dpad directions
are `0` or `1`. Mouse and gamepad `button_no` are 1-based. A pen or digitizer
row with `tip` 1 needs `in_range` 1. `axis_index` is the position of the axis in the gamepad axis
list (0-based). `wait_ms` is the delay after the row and can be left out,
which means `0`: rows with `0` are sent in the same report as the next row. A
`w` row sends nothing and only waits; it adds to the row before it, so `k,A,1`
followed by `w,50` is the same as `k,A,1,50`. Row prefixes are lowercase.
Every row runs at an absolute time
measured from the start of its lap, so a slow USB transfer never makes the
script drift. Keyboard modifiers (usages `0xE0`–`0xE7`) are always available,
whatever the usage range. libaoahid's keyboard profile is full N-Key
Rollover, so every usage in the configured range can be held down at
the same time with no limit on simultaneous keys. Media keys are different:
the toggle profile has a single Consumer Control field, so one media key is
held at a time; pressing another replaces it, and a `c` release (`down` 0)
releases whichever media key is held. See `csv/example.csv`.

## Laps: which rows run when

The script plays in laps, counted from 1. Rows keep the order they are
written in. A row outside a block runs on every lap; a row inside a block runs
on the laps the block names:

| block | runs on |
|-------|---------|
| `@lap A` | lap A (`@lap 1` is the first lap only) |
| `@lap A..B` | laps A to B |
| `@lap A..` | lap A and every later one |
| `@every N` | laps N, 2N, 3N, ... |
| `@every N from A` | laps A, A+N, A+2N, ... |
| `@every N from A to B` | the same, up to lap B |

A block ends with `@end`. Before it, `@else` starts the rows for the laps the
block does not run on:

```csv
@every 10
t,0,1,100,100,30
t,0,0,100,100,500
@else
w,530
@end
```

A lap is as long as the waits of what runs on it, so a lap that skips a block
is shorter by that block's time. `keep-time` after the block's laps (`@lap 1
keep-time`, `@every 10 keep-time`) makes the laps that skip the block wait its
time anyway; it cannot be combined with `@else`. Blocks do not nest.

Playback ends when no later lap has a row left: a script whose rows all sit in
`@lap 1..5` plays five laps. A lap with no row and no wait takes no time and is
passed over. *Loops* (`--loop N` in `aoa_touch`) count laps, the first
included; 0 repeats until stopped.

## Directives

A directive is a line starting with `@`. Names and options ignore case.

| directive | meaning |
|-----------|---------|
| `@format 3` | Optional. The format version; any other number is refused. Before the first row. |
| `@screen WxH` | The size of the coordinate space the touch, pen, and digitizer `x,y` are written in (each from 1 to 65536). Before the first row, once. |
| `@coords normalized` / `@coords integer` | With `normalized`, touch, pen, and digitizer `x,y` are fractions from 0 to 1 (`0.5` is the middle; a value outside 0–1 is an error). The default is `integer`. Pen and digitizer pressure stays an integer. Before the first row, once; `normalized` cannot be combined with `@screen`. |
| `@lap`, `@every`, `@else`, `@end` | Lap blocks; see [Laps](#laps-which-rows-run-when). |

## Coordinates

As each row is sent, its touch, pen, and digitizer positions are scaled from the
space the file names (`@screen` or `@coords normalized`) onto each connected
device's own touchscreen, pen, and digitizer surface, so devices with different resolutions play the
same file. Without a named space they are sent as they are, and a device they
do not fit skips the row.
A file written in a large square such as `@screen 32768x32768` therefore lands
in the same place on any phone. Each axis is scaled on its own, so a square
space is stretched to a phone that is not square. `@coords normalized` is
stored as a 65536 × 65536 space.

## Key names

`k` rows take a usage number or a name, ignoring case: `A`–`Z`,
`Digit0`–`Digit9`, `F1`–`F24`, `Enter` (`Return`), `Esc` (`Escape`),
`Backspace`, `Tab`, `Space`, `Minus`, `Equal`, `LBracket`, `RBracket`,
`Backslash`, `Semicolon`, `Quote`, `Grave`, `Comma`, `Period`, `Slash`,
`CapsLock`, `PrintScreen`, `ScrollLock`, `Pause`, `Insert`, `Delete`, `Home`,
`End`, `PageUp`, `PageDown`, `Up`, `Down`, `Left`, `Right`, `NumLock`, `Menu`,
and the modifiers `LCtrl`, `LShift`, `LAlt`, `LGui`, `RCtrl`, `RShift`,
`RAlt`, `RGui` (`Ctrl`, `Control`, `Shift`, `Alt`, and `Gui`, `Win`, `LWin`,
`Meta`, `Super` mean the left one; `RWin` is `RGui`). A bare number is always a usage number, so write digit keys as
`Digit1`.

## Media key names

`c` rows take a name, ignoring case, or the Consumer page (`0x0C`) usage
number: `Next` (`0xB5`), `Prev` / `Previous` (`0xB6`), `Stop` (`0xB7`),
`PlayPause` (`0xCD`), `Mute` (`0xE2`), `VolumeUp` (`0xE9`), `VolumeDown`
(`0xEA`), `Home` (AC Home, `0x223`), `Back` (AC Back, `0x224`), `New`
(AC New, `0x201`), `BrightnessUp` (`0x6F`), and `BrightnessDown` (`0x70`).
The brightness keys are not yet tested on a device.
The number `0x238` (AC Pan) is also accepted; Linux maps it
to a horizontal wheel step rather than a key. Any other usage is an error when
the file is loaded, because the toggle profile does not declare it. The rows
need the toggle profile (*Profiles* → toggle in the GUI, or `aoa_touch --toggle`).

```csv
c,VolumeUp,1,50
c,VolumeUp,0,500
c,PlayPause,1,50
c,PlayPause,0,0
```

## Checks

- Every problem in a file is reported with its file and line, up to 50 (then
  reading stops). A file with any problem is not played.
