# Wilidoro — Pomodoro Timer for the FreeWili 2 — Design

**Date:** 2026-07-24
**Status:** Approved design, pending implementation plan
**Target:** FreeWili 2 (RP2350B) · Pico SDK 2.x · LVGL 9 · wilibsp BSP

## Overview

Wilidoro is a native Pomodoro timer app for the FreeWili 2. It runs bare-metal
C on the RP2350B (polled superloop, no RTOS), renders its UI with LVGL 9 on
the 480×320 capacitive touchscreen, and uses the device's LEDs, audio, IMU,
ambient-light sensor, and CC1101 sub-GHz radio to make the classic
[Pomodoro Technique](https://en.wikipedia.org/wiki/Pomodoro_Technique) tactile
and fun. It ships three switchable visual themes and broadcasts a focus-state
beacon so nearby FreeWilis can see who is heads-down.

## Goals

- A polished, animated LVGL timer with three full themes: **Neon Arc**,
  **Retro Tomato Arcade**, and **Warm Flip Clock**.
- Physical, glanceable feedback: 16-LED progress bar, synthesized per-theme
  sounds, IMU gestures, ambient-light auto-dim.
- A CC1101 **focus-state beacon** plus a Nearby screen showing other devices.
- Fast development loop: a Windows SDL simulator target sharing all UI and
  logic code, and host unit tests for the pure-logic core.

## Non-goals

- No persistence: stats are session-only and reset at power-off (explicit
  decision; no SD/flash filesystem code).
- No synced group sessions over radio (beacon is broadcast + listen only).
- No WiFi/BLE/LoRa/NFC/IR involvement.
- No phone or host-PC companion.

## Platform decisions

- **Dev target:** native C + Pico SDK + LVGL 9, delivered as a UF2 /
  debug-probe-flashable image for the RP2350B.
- **Foundation:** [`wilibsp`](https://github.com/freewili/wilibsp) as a git
  submodule — bench-verified drivers for the ST7796 display, FT6336U touch,
  16× WS2812 LEDs, NAU88C10 I2S audio, CC1101 radio, BMI323 IMU, OPT4001
  light sensor — plus its `fw` CLI for build/flash/test and its documented
  hardware invariants (SPI1 arbitration, DMA_IRQ_0 ownership, pio2
  cohabitation, PCAL6524-gated rails).
- **LVGL glue:** LVGL 9 vendored under `third_party/lvgl`, wired to the BSP
  display/touch drivers following the pattern proven in
  [`sensorview`](https://github.com/freewili/sensorview) (RGB565 over SPI1 +
  DMA, polled touch, PWM backlight).
- **Simulator:** a host build using LVGL's SDL backend compiles the same
  `ui/` + `core/` sources on Windows. LEDs render as an on-screen strip;
  keyboard keys stand in for buttons and gestures; sliders/keys fake lux and
  IMU; the radio is a loopback stub with fake neighbors.

## Architecture

```
wilidoro/
  wilibsp/                git submodule (BSP + fw CLI)
  third_party/lvgl/       LVGL 9.x
  src/
    core/                 pure logic — no LVGL, no hardware, host-unit-tested
      pomodoro.c          session state machine + config + session stats
      beacon.c            beacon packet encode/decode + neighbor table
      gestures.c          IMU gesture classifier (flip, shake, pick-up) —
                          replaced by tilt.c, see the note below
      dimming.c           lux → backlight/LED brightness curve
    ui/                   LVGL screens + themes (compiles on host and target)
      screens/            timer_face, settings, nearby
      themes/             neon.c, arcade.c, flip.c (colors, fonts, widgets,
                          animations, sound set, LED patterns)
    hal/                  one small header per device; two implementations
      hal_display, hal_touch, hal_leds, hal_audio, hal_imu,
      hal_light, hal_radio, hal_clock
    target/               RP2350B main.c + HAL impl on wilibsp drivers
    host/                 SDL main.c + HAL stubs/fakes
  tests/host/             unit tests for core/ (no SDK, no hardware)
  docs/superpowers/       this spec + implementation plans
```

Layering rule: `core/` includes nothing but the C standard library and its
own headers. `ui/` talks to `core/` and LVGL only. Only `hal/`
implementations touch wilibsp or SDL. The target main loop is fully polled:
tick LVGL, tick the timer core, poll touch and sensors at modest rates,
service the radio, refresh LEDs.

## Timer core

Classic cycle, all durations configurable in Settings:

- **Focus** 25 min (default) → **short break** 5 min; every 4th focus earns a
  **long break** of 15 min.
- States: `IDLE`, `FOCUS`, `BREAK_SHORT`, `BREAK_LONG`, `ALARM`
  (ring-until-acknowledged), `PAUSED` (remembers what it paused).
- Focus end → `ALARM` (sound + LED celebration) until acknowledged by touch,
  any button, or a shake gesture; acknowledging starts the break
  automatically.
- Break end → gentle chime; the next focus requires an explicit start (tap,
  softkey, or flip face-down) so breaks never roll into unearned streaks.
- Softkeys during a session: **Pause/Resume · Skip · +5 min · Nearby · Menu**.
- Session-only stats: pomodoros completed, current streak (broken by skip or
  abandon), total focus minutes. The Arcade theme derives score, level, and
  combo multipliers from these for its celebrations; other themes show the
  plain numbers.

## Gestures (BMI323, classified in `core/gestures.c`)

*Superseded by `2026-07-25-wilidoro-C3-tilt-pause-design.md`: `core/gestures.c`
was replaced by `core/tilt.c` and the five-gesture table below by a single
tilt-to-pause rule. Kept here as a historical record.*

| Gesture | Context | Action |
|---|---|---|
| Flip face-down | Idle | Start a focus session |
| Flip face-down | Focus | Deep-focus: screen + LEDs off, timer keeps running |
| Flip face-up | Deep-focus | Restore display |
| Shake | Alarm ringing | Acknowledge/dismiss |
| Pick-up | Screen dimmed | Wake to full brightness |

## Screens

Three screens; softkeys navigate, BACK always returns to the timer face.

1. **Timer face** — per-theme. Neon Arc: glowing drain ring, thin digits,
   session dots. Arcade: pixel tomato mascot, health-bar progress, score
   line. Flip Clock: mechanical flip digits, warm palette, prose status.
   Small overlay icons indicate disabled features (see Error handling).
2. **Settings** — touch list with large targets: focus/short/long durations,
   long-break cadence, theme picker, volume, focus-tick on/off, beacon
   on/off, broadcast name (8 chars). DEFAULTS and SAVE softkeys. Settings
   are session-only (no persistence), defaults restored at boot.
3. **Nearby** — one row per heard device: name, colored state dot, state +
   minutes remaining, today's pomodoro count. Rows expire 60 s after the
   last beacon.

## Hardware behaviors

- **LEDs (16× WS2812):** progress bar filling across the session in the
  theme's palette; state-colored (focus warm, break cool, paused dim pulse);
  per-theme completion celebrations (Neon breathing glow, Arcade rainbow
  chase, Flip candle flicker). Brightness follows the dim curve.
- **Audio (NAU88C10):** all sounds synthesized (tone + envelope, no sample
  assets): per-theme start chimes and end alarms (Arcade chiptune jingle,
  Flip soft double-chime, Neon sine sweep), optional quiet focus tick,
  subtle touch blips. Master volume in Settings.
- **Auto-dim (OPT4001):** smoothed lux → backlight PWM with a comfort floor
  (sensorview's approach); LED brightness scales with it; in a dark room the
  alarm volume steps down one notch.
- **Beacon (CC1101):** every ~10 s broadcast, using wilibsp's radio engine:

  | Field | Size | Notes |
  |---|---|---|
  | magic | 2 B | protocol id |
  | version | 1 B | protocol version |
  | name | 8 B | ASCII, space-padded |
  | state | 1 B | idle / focus / break |
  | minutes remaining | 1 B | 0 when idle |
  | pomodoros completed | 1 B | this session |
  | CRC-16 | 2 B | packets failing CRC are dropped |

  Between transmissions the radio listens and feeds the neighbor table.
  Beacon off in Settings = radio fully idle. Exact modulation/bitrate
  follows whatever wilibsp's radio engine supports best (decided in the
  implementation plan, verified with a two-device test).

## Error handling

- Each peripheral init may fail independently; the app always boots. A
  failed peripheral disables its feature and shows a small crossed-out icon
  on the timer face (radio, IMU, light, audio).
- Radio: CRC-checked packets only; unknown magic/version ignored.
- The timer core depends only on `hal_clock`, which cannot fail.

## Testing

- **Host unit tests** (`tests/host/`, plain gcc, no SDK): state-machine
  transitions incl. pause/skip/+5/long-break cadence; beacon codec
  round-trips + malformed-input rejection; gesture classification from
  canned IMU traces; dim-curve mapping.
- **Simulator:** all UI, themes, animations, and screen flows iterated on
  Windows; fake neighbors exercise the Nearby screen.
- **On-hardware smoke checklist:** display + touch, LED bar + celebrations,
  each theme's sounds, gestures, auto-dim in bright/dark, and a two-device
  beacon exchange.

## Decisions log

- Native C + LVGL 9 UF2 (not WASM, not scripting) — chosen by user.
- All three themes ship, switchable at runtime — chosen by user.
- Stats are session-only, no persistence — chosen by user.
- CC1101 focus-state beacon (not synced group sessions) — chosen by user.
- PC simulator + device workflow — chosen by user.
- Foundation: wilibsp submodule + sensorview-pattern LVGL glue — chosen by user.
