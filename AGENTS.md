# For coding agents

Tactile changes how the trackpad of an Apple Silicon MacBook clicks under
Linux. This file is how to install it, check it, and use it on someone's
behalf. The README explains what it is.

## Before installing

All of these must hold. If one does not, stop and tell the person; do not
work around it.

```sh
uname -m                                  # aarch64
grep -qa '^apple,' <(tr '\0' '\n' </proc/device-tree/compatible) && echo mac
command -v makepkg pacman                 # an Arch-based system
test -d /sys/module/hid_magicmouse && echo trackpad-driver-loaded
test -e "/lib/modules/$(uname -r)/build/Module.symvers" && echo headers
grep -qw apple_taptic_send "/lib/modules/$(uname -r)/build/Module.symvers" && echo kernel-has-the-click
```

The last one is true on Omarchy's linux-aurora kernel and not, so far, on
Asahi's own. Without it the driver declines to build and nothing changes.

Missing headers: the package is the kernel's own name plus `-headers`
(`pacman -Qqo /usr/lib/modules/$(uname -r)/vmlinuz` names the kernel's).
Installing packages needs the person's password.

## Installing

```sh
git clone https://github.com/MadejaJiri/tactile ~/.local/share/tactile
~/.local/share/tactile/install.sh
```

`install.sh` must run in a terminal the person can type into. It asks for
their `sudo` password (three times on a first install), for pacman's
confirmation, and whether to switch the trackpad's driver now. Do not try to
answer the password prompts, and do not run it as root. If you have no such
terminal, give the person the two lines above to run and wait.

It ends with a list of checks, each `ok`, `..` (a note) or `!!` (a problem),
and exits 1 if there was a problem. Updating is `git pull` and `install.sh`
again.

## Checking

None of these needs root:

```sh
tactile-driver status      # installed and running should both say Tactile's driver, same version
tactile status --json      # "owner": "host" means Tactile's driver plays the click
tactile click              # plays one click; exit 0 means the driver took it
```

What `tactile status` can say about the click:

- `played by the driver`: working.
- `played by the trackpad's firmware`: Tactile is switched off (`hostClick=0`),
  or the kernel's own driver is loaded. `sudo tactile-driver reload` or a
  reboot loads Tactile's.
- `the actuator was not found at this boot`: a kernel bug, not something
  Tactile did; the firmware's click still works. See below.

## Using it

```sh
tactile keys               # every setting: range, default, meaning
tactile set press=80 release=70
tactile preset medium      # light, medium, firm, firmware
tactile skill install      # installs the skill that documents all of this
```

Every command takes `--json`, exits 0 or 1, and writes errors to stderr.
Settings apply at once and survive reboots.

## Leave these to the person

- `sudo tactile-driver repair`, `reload --probe`, `stock`, and
  `boot-repair on`. They probe the trackpad's controller again, which pauses
  the keyboard for two seconds and once froze the author's machine for four
  minutes. Explain the risk (`tactile-driver help`) and let them decide.
- Anything under `/usr/share/omarchy`, and the kernel itself.

## Removing

```sh
sudo pacman -R tactile     # then reboot; the kernel's own driver is back
```

## Working on the code

`bin/build`, `bin/test` (unit tests, offscreen), `bin/install` (makepkg).
`TACTILE_DEMO=1 build/tactile` opens the window with made-up presses and no
service. The driver in `driver/` is a patched copy of one kernel file and
refuses to build for a kernel it was not made for; `driver/README.md` has the
protocol notes.
