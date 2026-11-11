---
name: tactile
description: Read and change how the trackpad of an Apple Silicon MacBook clicks under Linux (click pressure, haptic feedback, Force Click) with the `tactile` command. Use when asked to make the trackpad click lighter or firmer, change its vibration, or check why the click feels wrong.
---

# Tactile

`tactile` sets how the trackpad clicks: how hard to press, what the click
feels like, and whether a firmer press gives a second click. Settings apply
at once and survive reboots. Every command takes `--json`, exits 0 or 1, and
writes errors to stderr. None of them needs a display or `sudo`.

```sh
tactile status                 # driver, trackpad, who plays the click, all settings
tactile keys                   # every setting: range, default, what it does
tactile get press              # one setting
tactile set press=80 release=70
tactile set hostClick=off      # the firmware plays the click again; on takes it back
tactile preset light           # light, medium, firm, or firmware
tactile click                  # play one click (press, release, or force)
tactile tune heartbeat         # play a rhythm; `tactile tune` lists them
tactile watch --seconds 5      # pressure, fingers, and "click" per report
```

## Settings

Pressures are the trackpad's own unit: a resting finger reads 30 to 60, the
firmware's built-in click sits near 125, a hard press peaks around 1000.

| Key | Meaning |
|-----|---------|
| `hostClick` | 1: the driver plays the click at the pressures below. 0: the trackpad's firmware does, at its own fixed pressure. |
| `press`, `release` | Pressure at which a click starts and ends. `release` stays at least 5 under `press`; 10% under is a good value. |
| `debounce` | Milliseconds the pressure must stay low to end a click. |
| `fingerPercent` | What each finger beyond the first adds to every threshold, in percent of `press`, so two fingers resting to scroll do not click. Default 100; 0 switches it off. |
| `oneFinger` | 1: a click starts only with one finger on the pad. This also stops the two-finger (right) click. |
| `strength`, `softness` | Bytes of the click's waveform, 1 to 255. 0 uses the driver's default for the model. |
| `releaseStrength` | Strength of the pulse on release. 0: the same as `strength`. |
| `pressPulses`, `releasePulses`, `pulseGap` | How many pulses a press and a release play, and the milliseconds between them. `pressPulses=0` makes the press silent. |
| `forceClick` | 1 adds a second, deeper click on a firm press. |
| `force`, `forceRelease` | Where that second click starts and ends. |
| `forceStrength`, `forceSoftness`, `forceWaveform`, `forcePulses`, `forceGap` | Its feel. `forceWaveform=0` reuses the press waveform. |
| `forceKey` | Key held while a Force Click lasts: `none`, or `f13` to `f19`. Bind it in the compositor by key code (F13 is `code:191`, F19 `code:197`). |

The presets set `press`, `release`, `strength` and `softness` together:
light 60/50/65, medium 120/110/105 (the default), firm 200/130/200, softness
160. A light click wants a gentle pulse and a firm click a strong one, so
when changing `press` alone, change `strength` along the same line.

`tactile set` refuses a value outside its range and keeps the pressures in
order, so the values it prints back are the ones in effect.

## When it does not work

- "service is not running": `sudo systemctl enable --now tactiled` (ask the user to run it).
- `tactile status` says the kernel's own driver is loaded: settings are saved
  but not in effect until Tactile's driver is loaded, by a reboot or
  `sudo tactile-driver reload`.
- "saved, but the loaded driver is older than this Tactile": some settings
  wait for the new driver, which loads at the next boot or with
  `sudo tactile-driver reload` (ask the user to run it).
- "the actuator was not found at this boot": the part that makes the click
  did not come up (seen during development, not since 0.2.1); the firmware's
  click still works. `tactile-driver help`
  explains the repair and its risk. Do not run it unasked.
- "the trackpad refused to hand over the click": it only switches while no
  finger is on it. Ask the user to lift their hand and touch the pad once.

`tactile` with no arguments opens the settings window with a live pressure
chart; that needs the user's desktop.
