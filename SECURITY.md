# Security Policy

## Supported Versions

Only the [latest release](https://github.com/nemarpuc/aoahid_player/releases/latest)
receives security fixes. Before 1.0.0 a fix may come with a change of the minor
number.

## Scope and Threat Model

- **The player injects input.** A script plays whatever input it contains (touches, keys,
  gamepad) into the connected phone. Run only scripts you trust.
- **Recordings can hold typed text.** `aoa_record` and the GUI's recorder save real touches
  and key presses. Do not publish a recording made while a password or private message was
  typed, and do not attach one to a public issue; reproduce with synthetic input.
- **Control API.** The GUI's local HTTP control API listens on `127.0.0.1` only and has no
  authentication: any program on the machine can reach it once it is switched on. A request
  with an `Origin` header, or with a `Host` that is not `127.0.0.1:<port>` or
  `localhost:<port>`, is refused, which blocks web pages. See the
  [Control API](docs/CONTROL_API.md) document.
- **ADB Bridge.** The bridge opens a port on `127.0.0.1` that reaches the phone's ADB, which
  can run a shell. Any local process can connect to it while it is on.
- **USB device access.** On Linux the udev rule in `udev/` decides who may open the phone.
  Review it against your machine's user and group policy before installing.

## Reporting a Vulnerability

Report vulnerabilities privately through the repository's
[GitHub Security Advisories form](https://github.com/nemarpuc/aoahid_player/security/advisories/new).
Do not put device serial numbers, ADB keys, or recordings that hold real input in a public issue.
