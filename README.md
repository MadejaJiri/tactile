# Tactile

![Tactile showing the live pressure chart and the click settings](docs/screenshot.png)

A light, tunable trackpad click for Apple Silicon MacBooks under Linux, with
the trackpad's own haptic feedback.

The trackpad of a MacBook does not move. A click is the pad noticing enough
pressure and firing the actuator underneath. Under Linux that pressure is fixed
by the trackpad's firmware, and it is a good deal heavier than the "Light"
setting in macOS. Tactile lets the driver play the click instead, at the
pressure you choose, with one vibration per press.

It is three parts:

- a patched copy of the kernel's `hid-magicmouse` driver, built for your
  kernel by DKMS, so it survives reboots and kernel updates;
- `tactiled`, a service that keeps your settings and applies them at boot;
- `tactile`, a settings window with a live chart of how hard you press, and
  the same thing as commands.

## Status

Written and tested on one machine: a MacBook Pro 14" M1 Pro (2021, `apple,j314s`)
running [Omarchy](https://omarchy.org) with the linux-aurora 7.2.11 kernel.
Other Apple Silicon MacBooks use the same driver, and the patch only changes
what happens on trackpads that hide the report in question, but nobody has
tried another model yet. If you do, please open an issue and say how it went,
either way.

The driver refuses to build for a kernel it was not made for (see
[Safety](#safety)), so on a kernel that differs too much you keep the stock
click instead of a broken trackpad.

## Install

On Omarchy or another Arch-based system on Apple Silicon, with the headers of
your running kernel installed (`linux-aurora-headers` or `linux-asahi-headers`):

```sh
git clone https://github.com/MadejaJiri/tactile
cd tactile
./install.sh
```

`install.sh` builds the package, installs it with `pacman`, starts the service,
and checks that the driver is built for your kernel and is in the boot image.
It asks for your password through `sudo` and tells you what did not work, if
anything. Then open **Tactile** from the launcher (`Super + Space`).

To remove it: `sudo pacman -R tactile`, then reboot. The kernel's own driver is
back and nothing else is left behind except your settings in `/var/lib/tactile`.

## The window

The chart shows the summed pressure of your fingers over the last eight
seconds, on the trackpad's own scale: a resting finger reads 30 to 60, the
firmware's built-in click sits near 125, a firm press peaks around 1000. The
dashed lines are where a click starts and ends; drag them, or use the sliders.
Each press is labelled with its peak, and the line above the chart tells you
how much harder you press than a click needs.

- **The switch at the top** decides who plays the click: Tactile, with
  everything below, or the trackpad's firmware exactly as without Tactile.
- **Presets**: Light, Medium and Firm set the click pressure together with the
  feedback that suits it. Medium is the default, and what Reset goes back to.
  Change anything and the fourth button, Custom, lights up.
- **Click**: the pressure at which a click starts, the pressure at which it
  ends (the slider stops below the click pressure, and a tick marks 10%
  under it), and how long the pressure has to stay low before it counts as
  released.
- **More than one finger**: the pad adds up the pressure of all fingers, so
  two fingers resting to scroll can click by accident. You can make every
  extra finger raise the click pressure, or let only one finger start a click.
- **Feedback**: how strong the pulse is, its shape, a different strength on
  release, and how many pulses a press and a release play. The small chart
  shows the pulses against time. A light click wants a gentle pulse and a firm
  click a strong one: a tick on the strength slider marks what suits the click
  pressure, and Match sets it. Tactile never changes the feedback by itself.
- **Force Click**: a second, deeper click when you press harder, like macOS.
  It can hold one of the keys F13 to F19 while it lasts; bind that key in
  your compositor and Force Click does whatever you like.
- **Calibrate** watches your finger glide, then five presses as light as you
  want a click to be, and sets the pressure between the two, with the
  feedback that suits it.
- **Play a rhythm**: knocks, a heartbeat, a drum roll. For fun.

| Key | Action |
|-----|--------|
| `T` | Tactile on or off |
| `1` `2` `3` | Light, Medium, Firm |
| `[` `]` | Click pressure down, up |
| `M` | Match the feedback to the click pressure |
| `Space` | Feel the click |
| `C` | Calibrate |
| `F` | Force Click on or off |
| `P` | Pause the chart |
| `R` | Reset everything to Medium |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo, redo |
| `?` | Show the shortcuts |
| `Q` | Quit |

Every change applies at once and is saved; there is no Apply button. The window
follows the Omarchy theme and language (English and Czech so far).

### Giving Force Click an action

Pick a key under Force Click, say F13, and bind it in your compositor. Bind
it by its key code rather than its name: keyboard layouts call F13 to F18
`XF86Tools` and `XF86Launch5` to `XF86Launch9`, while the code is the same
everywhere. The window shows the code for the key you picked: 191 for F13 up
to 197 for F19.

In a Hyprland config:

```
bind = , code:191, exec, notify-send "Force Click"
```

On Omarchy the same goes into `~/.config/hypr/bindings.lua`, in the form of
the bindings already there:

```lua
o.bind("code:191", "Force Click", "notify-send 'Force Click'")
```

The key goes down when Force Click starts and up when it ends.

## Commands

Everything the window does, without it:

```sh
tactile status                 # driver, trackpad, who plays the click, all settings
tactile keys                   # every setting: range, default, what it does
tactile get press
tactile set press=80 release=70
tactile set hostClick=off      # hand the click back to the firmware; on takes it again
tactile preset light           # light, medium, firm, or firmware
tactile set fingerExtra=40     # each extra finger adds 40 to the click pressure
tactile set oneFinger=on       # or: only one finger starts a click
tactile set forceKey=f13       # Force Click holds F13; forceKey=none for no key
tactile click                  # play one click (press, release, or force)
tactile tune                   # the rhythms; tactile tune heartbeat plays one
tactile watch --seconds 5      # the pressure as it changes
tactile skill install          # teach a coding agent to use the above
```

Each takes `--json` and exits 0 or 1. None needs `sudo`.

| Setting | What it is |
|---------|------------|
| `hostClick` | 1: Tactile's driver plays the click. 0: the trackpad's firmware does |
| `press`, `release` | pressure at which a click starts and ends; `release` stays at least 5 under `press` |
| `debounce` | ms the pressure must stay low before the click ends |
| `fingerExtra` | pressure each finger beyond the first adds to every threshold |
| `oneFinger` | 1: a click starts only with exactly one finger on the pad |
| `strength`, `softness` | the two bytes of the click's waveform, 1 to 255 (0: the driver's default) |
| `releaseStrength` | strength of the pulse on release (0: same as `strength`) |
| `pressPulses`, `releasePulses`, `pulseGap` | pulses on press (0: silent) and on release, and the ms between them |
| `forceClick` | 1: a harder press gives a second, deeper click |
| `force`, `forceRelease` | where that click starts and ends |
| `forceStrength`, `forceSoftness`, `forcePulses`, `forceGap` | its feel |
| `forceWaveform` | 1: the trackpad's own Force Click waveform. 0: the click's |
| `forceKey` | key held during a Force Click: `none`, or `f13` to `f19` |

The driver itself:

```sh
tactile-driver status             # which driver is installed and which is loaded
sudo tactile-driver reload        # load the installed driver now, without a reboot
sudo tactile-driver stock         # the kernel's own driver until the next boot
sudo tactile-driver repair        # find the actuator again if a boot missed it
sudo tactile-driver boot-repair on   # do that at every boot that needs it (off by default)
```

`reload` swaps only the trackpad's driver: the trackpad pauses for a moment
and the keyboard keeps working.

### The actuator that goes missing

On the machine above, three of five boots did not find the actuator, the part
that makes the click, with or without Tactile. The firmware still clicks then,
but the driver has nothing to play its own click with, and Tactile says so.
The cause is in the kernel's SPI transport, which asks for each device's
description once and does not ask again when the answer is lost.

Probing the trackpad's controller again (`repair`) brings the actuator back,
at the price of a two-second pause of keyboard and trackpad. It is not run
automatically: one such probe out of nine froze the whole machine for four
minutes, for a reason not yet found. `sudo tactile-driver boot-repair on`
runs it at every boot that needs it, if you accept that.

## How it works

The kernel's driver can already play the click itself (`haptic_press=1`), but
on this trackpad a firm press then vibrated twice: once from the driver, and
once more from the firmware, which never stopped clicking on its own. The
request that tells the firmware to stop was silently never sent, because the
trackpad does not announce that it understands it. It does understand it. The
patch sends it, checks that it took, and only then starts playing clicks; the
whole story, with the protocol notes, is in [driver/README.md](driver/README.md).
That fix is `driver/0001-…patch`, written to go upstream. What Tactile adds
on top for its settings (the rule for extra fingers, the Force Click key, a
strength of its own for the release) is `driver/0002-…patch`.

`tactiled` runs as root with a tight systemd sandbox, because the driver's
options and the trackpad's event device belong to root. The window and the
commands talk to it over a socket in `/run/tactile`. It answers only the user
sitting at the machine (an active local session), accepts only known settings
within their ranges, and forwards three numbers from the trackpad: the summed
pressure, the number of fingers, and whether the button is down. Finger
positions never leave the service, and it does not touch the keyboard.

## Safety

- The firmware's own click is the fallback everywhere. Until the driver has
  confirmed that the trackpad handed the click over, and whenever that fails,
  the stock button and click pass through untouched.
- The patched driver shares private structures with code inside the kernel.
  Before every build, `driver/abi-check.sh` compares the kernel against what
  the patch was made from, and builds nothing if they differ. After a kernel
  update that changes the driver you get the stock click and a line in
  `tactile-driver status`, not a crash. That also means a new kernel can need
  a new Tactile.
- The patch adds one thing the driver says to the trackpad: who plays the
  click. It writes that only while no finger is on the pad, reads it back,
  and hands the click back to the firmware when the driver is unloaded.
- It is a kernel module from outside the kernel tree. It works on the machine
  above; it comes with no warranty.

## Building

```sh
bin/build      # build/tactile and build/daemon/tactiled
bin/test       # the unit tests, offscreen
bin/install    # makepkg -fsi
```

`TACTILE_DEMO=1 build/tactile` opens the window with made-up presses and no
service, which is how the screenshot above was taken.

## Licence

The app, the service, and the tools are MIT. The driver in `driver/` is
GPL-2.0-or-later, like the kernel file it patches. See [LICENSE](LICENSE).
