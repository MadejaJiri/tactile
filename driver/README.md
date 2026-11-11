# The driver: a light click with one vibration

A patched `hid-magicmouse` for Apple Silicon MacBooks under Linux, worked out on
a MacBook Pro 14" M1 Pro (`apple,j314s`) running linux-aurora 7.2.11. This is
the part of Tactile that runs in the kernel; the [main README](../README.md)
covers installing and using it.

It gives the trackpad a click as light as macOS's lightest setting, with the
pad's haptic feedback, and exactly one vibration per press however hard the
press is.

## The problem

The trackpad does not move. A "click" is the firmware noticing enough pressure
and firing the actuator under the pad. The firmware's threshold is fixed (a
summed finger pressure of about 125 on this machine), which is a much heavier
click than macOS's "Light" setting.

The driver can take the click over (`haptic_press=1`): it derives the button
from the pressure at its own threshold and plays the click waveform itself. With
a threshold of 60 that gives the light click, but every firm press vibrated
twice: once at 60, and again where the firmware's own click sits.

## The cause

Two separate things, both of which vibrate on a hard press.

**1. The firmware never stopped clicking.** Before the driver plays its own
click it has to tell the actuator to stop clicking by itself. That is feature
report `0x21`: `21 00` leaves the click to the firmware, `21 01` hands it to the
host. On this trackpad the actuator's HID descriptor declares only input `0x3f`
and output `0x53`, and nothing about `0x21`. The helper module that sends the
request (`hid-apple-mtp-haptic`, `apple_taptic_switch_modes()`) looks the report
up in the descriptor first, does not find it, and returns success without
sending anything. So the driver believed it owned the click and the firmware
kept playing its own.

The firmware does take the report. Written as a feature report it is accepted
and reads back; written as an output report it is rejected.

**2. The driver's own deep click.** Upstream's click has three levels (up,
clicked, deep) and plays a second, stronger waveform at
`haptic_deep_threshold` (default 500), like macOS's Force Click. Firm presses
here peak between 450 and 1100, so they crossed it. No key is bound to the deep
click by default, so all it did was vibrate again.

## What the patch does

[0001-hid-magicmouse-one-click-per-press.patch](0001-hid-magicmouse-one-click-per-press.patch),
against omacom/linux-aurora @ `78742b7013`. The patched file is
[hid-magicmouse.c](hid-magicmouse.c).

- For SPI actuators whose descriptor lacks report `0x21`, the driver writes the
  mode itself as a feature report and reads it back to be sure. Every other
  actuator goes through the helper exactly as before.
- The mode is switched from a delayed work item, only while nothing touches the
  pad, with retries and backoff. It is read again every 30 seconds of use and
  after a controller reset or resume, and taken again if the firmware dropped
  it.
- There is one click source at a time. The driver plays waveforms and derives
  the button only while host mode is confirmed. Before that, and whenever the
  switch fails, the firmware's own button and click pass through untouched, so
  the worst case is the stock behaviour.
- The click goes back to the firmware when the driver is unloaded, and once
  `haptic_press` is turned off.
- The derived click is debounced (`haptic_debounce_ms`). The actuator shakes
  the pad, the pressure dips for a few milliseconds, and without this a press
  could register as two clicks.

The deep click is not removed. Tactile turns it off with
`haptic_deep_threshold=0` unless Force Click is switched on, and then it is one
deliberate second click at a pressure you set.

## Evidence

A guided test: three firm presses per round, the person reports how many vibrations a press gave on the
way down, and the firmware's button bit and the actuator traffic are logged
alongside.

| Round | Setup | Vibrations |
|---|---|---|
| 1 | firmware mode, host click off | 1 (firmware button on at pressure 125-134) |
| 2 | host mode written, host click off | 0 |
| 3 | firmware mode, firmware waveforms `0x22`/`0x23` zeroed | 0 |
| 4 | host mode, host click on at 60, deep click off | 1 |

Round 2 shows that `21 01` is what silences the firmware. Round 4 is the goal.

The patched driver taking the click by itself, as a sniffer on the SPI
transport recorded it right after the driver was loaded (seconds since boot;
the controller had just been power cycled):

    12143.456  GET feature 0x21          -> 21 00    firmware owns the click again
    12143.459  SET feature 0x21 = 21 01  -> accepted
    12143.480  GET feature 0x21          -> 21 01    verified
               kernel log: "click feedback is host-driven"

The presses that followed played one click and one release waveform each, no
deep-click waveform, and the firmware rejected nothing.

## Protocol notes (J314 actuator)

Reports the actuator answers to a feature GET are listed with their values in
[j314-actuator-feature-reports.txt](j314-actuator-feature-reports.txt). The ones
whose meaning is known:

| Report | Meaning | Bytes |
|---|---|---|
| `0x21` feature | who plays the click | `21 00` firmware, `21 01` host |
| `0x22` feature | firmware's press waveform | `22 01 23 a8 03 06 22 48 49 01 09 60 17 2d 03 05 18 32 1e` |
| `0x23` feature | firmware's release waveform | `23 01 13 90` |
| `0x53` output | play a waveform now | `53 01 SS FF ...` (strength, softness, pattern) |

- `0x21` is accepted only as a feature SET (transport message type `0x52`). As
  an output report (`0x51`) the firmware rejects it.
- A new mode shows in the very next GET, 4 to 8 ms later. No spacing between
  requests is needed.
- The firmware's reply to a SET carries a status byte: `00` accepted, `04`
  rejected. The transport driver does not look at it, and it drops zero-length
  replies, so a rejected SET is invisible to the HID layer and a rejected GET
  shows up as a one-second timeout (`-110`).
- The transport copies `max(len, reply_len)` bytes out of a 512-byte reply
  buffer, so a feature GET should ask for exactly 512 bytes.
- The actuator holds its mode while the pad is in use, but not across a power
  cycle of the controller: after the SPI device is bound again it reads `21 00`.
  The driver asks first and writes only when the answer is not the one it
  wants.

## Build

Installing Tactile does this through DKMS. By hand, with the kernel's headers,
`make`, `gcc` and `pahole` installed, and without root:

    make -C driver KDIR=/lib/modules/$(uname -r)/build

[abi-check.sh](abi-check.sh) runs first and refuses to build unless the kernel
matches what the patch was made from in three ways: it exports the helper
functions the driver calls, its own `hid-magicmouse` has the same options, and
the two structures the driver shares with built-in code are laid out the same
([abi/](abi/)). There is no stable interface between a kernel and one of its
drivers, so a mismatch has to mean "keep the stock driver", not "try anyway".

## Parameters

All under `/sys/module/hid_magicmouse/parameters/`, writable by root at runtime.

| Parameter | Tactile's default | Meaning |
|---|---|---|
| `haptic_press` | 1 | the driver plays the click (0: the firmware does) |
| `haptic_press_threshold` | 60 | pressure at which a click starts (upstream 120) |
| `haptic_release_threshold` | 35 | pressure at which it ends (upstream 75) |
| `haptic_debounce_ms` | 40 | how long the pressure must stay low to end a click (new) |
| `haptic_deep_threshold` | 0 | pressure for the deep click; 0 is off, 500 gives a Force Click |
| `haptic_trace` | 0 | log every click transition and mode check (new) |

For reference: a light touch reads 28 to 60, the firmware clicks near 125,
a firm press peaks at 450 to 1100.

## Open points

- Suspend and resume with the patched driver loaded has not been tried yet.
- Whether the firmware accepts a mode change while a finger is pressing was not
  tested; the driver only switches while the pad is idle.
- Only tested on J314. Other SPI trackpads whose descriptor hides `0x21` take
  the new path; trackpads that declare it are unaffected.
- Reported upstream as
  [linux-aurora#446](https://github.com/omacom/linux-aurora/issues/446): the
  helper returning success without sending anything, the feature-versus-output
  finding, and the transport ignoring the reply's status byte.
- During development the actuator was missing after three of five boots. It
  has not been since 0.2.1, and the cause was never found
  ([linux-aurora#447](https://github.com/omacom/linux-aurora/issues/447),
  closed as no longer reproducing).
