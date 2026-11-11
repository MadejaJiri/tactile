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
tactile keys                   # every setting with its range and default
tactile get press              # one setting
tactile set press=80 release=50
tactile preset light           # light, medium, firm, or firmware
tactile click                  # play one click (press, release, or force)
tactile watch --seconds 5      # pressure, fingers, and "click" per report
```

## Settings

Pressures are the trackpad's own unit: a resting finger reads 30 to 60, the
firmware's built-in click sits near 125, a hard press peaks around 1000.

| Key | Meaning |
|-----|---------|
| `hostClick` | 1: the driver plays the click at the pressures below. 0: the trackpad's firmware does, at its own fixed pressure. |
| `press`, `release` | Pressure at which a click starts and ends. `release` stays at least 10 under `press`. |
| `debounce` | Milliseconds the pressure must stay low to end a click. |
| `strength`, `softness` | Bytes of the click's waveform, 1 to 255. 0 uses the driver's default for the model. |
| `pressPulses`, `releasePulses`, `pulseGap` | How many pulses a press and a release play, and the milliseconds between them. `pressPulses=0` makes the press silent. |
| `forceClick` | 1 adds a second, deeper click on a firm press. |
| `force`, `forceRelease` | Where that second click starts and ends. |
| `forceStrength`, `forceSoftness`, `forceWaveform`, `forcePulses`, `forceGap` | Its feel. `forceWaveform=0` reuses the press waveform. |

`tactile set` refuses a value outside its range and keeps the pressures in
order, so the values it prints back are the ones in effect.

## When it does not work

- "service is not running": `sudo systemctl enable --now tactiled` (ask the user to run it).
- `tactile status` says the kernel's own driver is loaded: settings are saved
  but not in effect until Tactile's driver is loaded, by a reboot or
  `sudo tactile-driver reload`.
- "the trackpad refused to hand over the click": it only switches while no
  finger is on it. Ask the user to lift their hand and touch the pad once.

`tactile` with no arguments opens the settings window with a live pressure
chart; that needs the user's desktop.
