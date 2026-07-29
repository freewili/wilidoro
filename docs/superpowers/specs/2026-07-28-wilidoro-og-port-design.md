# Wilidoro on the FreeWili OG — port design

**Date:** 2026-07-28
**Status:** Plan A (the Neon Arc theme relaid out for 320x240, the 5-button
softkey mapping, the panel bring-up) is implemented and **hardware-verified**.
Plan OG-B tasks 1-3 (the 7-LED WS2812 ring, and synthesized chimes over I2S)
are also implemented and **hardware-verified**. All of Plan OG-C (the two
remaining themes — Retro Tomato Arcade and Warm Flip Clock — live theme
switching, the Settings screen driven from the arrow pad, the Nearby screen,
and the SDL simulator's OG mode) is **built but has only ever been exercised
in the SDL simulator, never on a board**. Plan OG-B task 4 (tilt-to-pause over
the LIS3DH) and Plan OG-D (the sub-GHz beacon over the CC1101) remain to be
built — `hal_imu()` and `hal_beacon_rx()` both hard-return `false` on this
board today.

Port wilidoro to the FreeWili OG (FreeWili 1 / Classic) on top of
[`wiliOGbsp`](https://github.com/freewili/wiliOGbsp), as a second board target
inside the existing wilidoro repository.

## Why this is cheap

wilidoro's central rule — *the HAL is the only hardware seam* — is what makes
this port small. `src/core/` and most of `src/app/` include no LVGL, no BSP
and no SDL header, and none of that code changes for the OG:

| Shared verbatim | Lines |
|---|---|
| `src/core/` — pomodoro FSM, beacon codec, tilt gate, dimming curve | ~340 |
| `src/app/` — app_model, timer_view, led_pattern, sound | ~300 |

Roughly 640 shared lines against ~1400 board-specific. The shared slice is the
part with 11 host test binaries over it, and it is the reason this is one repo
rather than two.

## Hardware delta

| | FreeWili 2 | FreeWili OG |
|---|---|---|
| MCU | 1× RP2350B @ 250 MHz | **2× RP2040** @ 200 MHz (display + main) |
| LCD | ST7796 480×320 | ST7789 **320×240** |
| Touch | FT6336U | **none** |
| Buttons | 14 (5 softkeys + D-pad) | **5** (red also = power-off hold) |
| LEDs | 16× WS2812 | **7× WS2812** |
| IMU | BMI323 | LIS3DH (accel only) |
| Ambient light | OPT4001 | **none** |
| DVI out | HSTX 640×480p60 | **none** (RP2040 has no HSTX) |
| Audio | NAU88C10, tone HAL | I²S **8 kHz / 16-bit**, buffer API |
| Sub-GHz | 1× CC1101, same MCU | **2× CC1101**, on the *main* CPU |
| RAM | ample | **264 KB** |
| — | — | *unused here:* RTC, battery gauge, IR, PDM mic, FPGA |

## Decisions

1. **One repo, two board targets.** Extend wilidoro; add `wiliOGbsp` as a
   second submodule. `PICO_BOARD` is a single global cache value and the two
   boards are different silicon, so one configure cannot build both:
   `-DWILIDORO_BOARD=fw2|og` selects, each with its own preset and build dir.
2. **LVGL 9 is ported to the OG** rather than replaced by direct ST7789
   drawing. Keeps all three themes, the `theme_t` interface, and the
   Settings/Nearby screens as LVGL code, relaid out for 320×240.
3. **Softkeys stay 1:1.** Grey..red drive columns 0–4 exactly as on FW2. Red
   short-press remains a normal softkey; only a ~6 s hold reaches power-off.

   *Corrected during Plan OG-C planning:* this decision originally said Settings
   and Nearby would need a *Back* softkey on their rightmost column to replace
   the FW2's HOME/CANCEL buttons. They do not — both screens already put **Back
   on column 0** and route it to the timer, and neither ever depended on
   HOME/CANCEL. No change is needed on either screen. Adding a Back on column 4
   would have been actively wrong: on Settings that column is **Save**.
4. **No dimming.** No light sensor exists; `caps.light=false`, backlight
   pinned at 100. `dimming.c` stays in core for FW2's use.
5. **The beacon ships in v1**, across both CPUs, using the CC1101 hardware
   packet engine rather than wilidoro's software OOK.
6. **The SDL sim gains an OG geometry mode** so themes can be laid out on a
   desktop before a board is involved.

## Repository shape

```
src/core/            shared, unchanged
src/app/             shared; dvi_view.c is fw2-only
src/ui/              shared theme_t interface + screens; geometry per board
src/hal/             hal.h        the seam, extended
                     hal_target.c FW2 (unchanged)
                     hal_og.c     OG display CPU        (new)
                     hal_sim.c    both, geometry-switched
src/target/          FW2 device main
src/target_og/       OG display-CPU main + lvgl_port_og.c   (new)
src/main_og/         OG main-CPU radio app                  (new)
```

The OG build produces **two** executables: `wilidoro_display` (links
`fwog_display_bsp`) and `wilidoro_main` (links `fwog_main_bsp`, embeds the
display image via `FWOG_DISPLAY_FIRMWARE=wilidoro_display`). The display app
must be declared to CMake *before* the main app — `fwog_embed_display_image()`
resolves the target at configure time.

Submodule URL: `https://github.com/freewili/wiliOGbsp.git`.

### BSP constraints the implementation must honour

Taken from `wiliOGbsp/AGENTS.md`. Each of these is a build failure, a brick,
or a silent corruption.

- `fwog_display_app(... VERSION 001 DESCRIPTION "...")` and
  `fwog_main_app(...)`. Three-digit version, description required — missing or
  malformed is a configure error.
- `FWOG_POWER_DEFAULT()` must be declared or the display app **does not
  link**. `fwog_power_poll(now_ms)` is called once per loop and buttons are
  taken **from its return value** — a second `fwog_buttons_poll()` destroys
  the debounce state the 6-second power hold depends on.
- Never `printf`. UART0 is the inter-CPU link on both CPUs; use `DIAG()`.
- Never add a watchdog to the display CPU. (The main CPU may have one.)
- Never hardcode a PIO divider or baud rate; derive from `clock_get_hz()`.
- Main apps call `fwog_display_update_run()`, never `board_release_display()`.
- **Never flash the display app by UF2.** See below.

### Flashing

```sh
fw flash wilidoro_main       # correct, always
fw flash wilidoro_display    # BRICKS THE DISPLAY CPU
```

A display app's UF2 links at `0x10021000` and a raw copy writes only that, not
the metadata sector at `0x10020000` that only the serial update path writes.
The bootloader's `app_valid` check then fails, the app never runs, and the
display CPU — the one CPU with no BOOTSEL button — stops enumerating.
Recovery is flashing a main app whose `FWOG_DISPLAY_FIRMWARE` names the
display app you want.

This must be enforced in `tools/`, not merely documented.

### Tooling

`wiliOGbsp/tools/fw.py` computes its repo root from its own path, so as a
submodule it targets the BSP's build tree, not ours. We add thin
`tools/*.ps1` wrappers that configure and build our tree, and shell out to the
submodule's `fw.py` only for genuinely board-level operations (1200-baud
BOOTSEL, flash, console).

## HAL: `hal_og.c`

`hal_caps_t` for the OG: `radio=true, imu=true, light=false, audio=true,
buttons=true, leds=true, dvi=false`. `hal_caps()` reports absent hardware
honestly, but nothing on the timer face surfaces it -- `hal_caps()`'s only
reader is `screen_settings.c`, which prints a text string (e.g. "no imu") and
is compiled out entirely on the OG (Settings is not yet ported). Surfacing
capability gaps on the OG's timer face is later-plan work, not something that
already exists.

| Entry point | OG implementation |
|---|---|
| `hal_lux` | returns `false` |
| `hal_dvi_surface` | returns `false`; `hal_dvi_enable` is a no-op |
| `hal_backlight` | `board_backlight()`, pinned at 100 |
| `hal_imu` | LIS3DH `int16` triple → g-units float |
| `hal_tone` | synthesize band-limited tone → `i2s_audio_start` |
| `hal_next_button` | `fwog_power_poll()` return value; only `GREY..RED` |
| `hal_led_*` | `ws2812_set_color` / `ws2812_process`, 7 pixels |
| `hal_beacon_tx/rx` | link frames to the main CPU (see below) |

**IMU axis orientation is a bench-measured constant.** Which axis reads "flat"
differs from the BMI323 and is not derivable from the datasheet. Tilt-to-pause
cannot be completed without a board.

**Audio.** I²S runs at 8 kHz / 16-bit mono, so Nyquist is 4 kHz while
`SOUND_HZ_MAX` is 3000. Notes above ~2.5 kHz are clamped **in the OG HAL**,
not in `sound.c`, because the per-theme note tables are shared with FW2. A
400 ms note is 3200 samples (6.4 KB), well within what `i2s_audio_start`
accepts; `i2s_audio_process` handles A/B chaining. The chimes may simply sound
worse than on FW2; retune the tables if so.

**LEDs.** `led_pattern_render()` gains an explicit count parameter rather than
using the `LED_COUNT` macro, so the shared host tests can cover **both** n=7
and n=16. The progress fill is already proportional
(`elapsed * count / 1000`), so no logic changes.

**PIO.** `ws2812_init(pio0, 0)` and `i2s_audio_init(pio0, 2)`, following the
BSP's documented allocation. We need neither PDM nor IR, so there is no
contention.

## UI

New `lvgl_port_og.c` binds LVGL to `st7789_set_window` / `st7789_blit`, with
two 320×40 partial buffers. The display flush stays blocking, as on FW2.

Themes are relaid out from 480×320 to 320×240 — same faces, same palettes,
same `theme_t` interface. The large `MM:SS` face drops from Montserrat 48 to
Montserrat 40: `config/lv_conf.h` declares `LV_FONT_MONTSERRAT_40`. That font
is enabled **unconditionally for every target**, not per board:
`config/lv_conf.h` cannot see `WILIDORO_BOARD_OG` (it is a `PRIVATE` compile
definition on the `wilidoro_display` target, and `lvgl` is a separate target
that never gets it). The FW2 build links the extra font in but never
references it, and `--gc-sections` drops the unused glyph data from that
binary, so this costs the FW2 nothing -- it is not the one-line-per-board
switch it might sound like.

### RAM budget (RP2040, 264 KB)

| | KB |
|---|---|
| LVGL partial buffers (2× 320×40×2) | 51 |
| LVGL heap | 48 |
| Link RX buffer (`FWOG_LINK_MAX_PAYLOAD` = 4160) | 4 |
| I²S note buffer + driver A/B | ~14 |
| app / core / stacks | ~15 |
| **total** | **~132** |

Comfortable, but **measured first, not last**. The LVGL heap is the knob if a
theme overruns it.

## Beacon across two CPUs

The display CPU has no radio, so `hal_beacon_tx/rx` become messages over the
inter-CPU UART link, riding the BSP's existing host-tested framing
(`fwog_link_rx_byte`: SOF `0x7E`, length, CRC). Two new app-level opcodes,
following the convention `io_proto` uses:

```
0x40  BEACON_TX   display -> main   16-byte beacon_pack() frame, transmit it
0x41  BEACON_RX   main -> display   16-byte frame just received off the air
```

**Display side** (`hal_og.c`): `hal_beacon_tx` writes one `0x40` frame;
`hal_beacon_rx` drains inbound frames and returns any `0x41` payload. Nothing
above the HAL changes — `beacon_rx.c` and the Nearby screen are untouched.

**Main side** (`src/main_og/`): owns both CC1101s. The radio on `CS0`
transmits; the radio on `CS1` sits permanently in RX. A `0x40` frame becomes
`cc1101_send_packet(radio_a, wire, 16)`; a successful
`cc1101_receive_packet` on radio B becomes a `0x41` frame upward. It also runs
`fwog_display_update_run()` at boot.

### Why this should work where FW2's did not

wilidoro's beacon is the one feature its README admits was never confirmed over
the air: it bit-banged OOK, with `beacon_ook_encode` producing durations, a PIO
capturing edges, and a software framer reassembling them.

The OG uses the CC1101's own packet engine instead — 2-FSK, fixed 16-byte
length, sync word, hardware CRC, whitening. `beacon_ook_encode`/`decode` remain
compiled for FW2 only. What survives is `beacon_pack`/`beacon_unpack`: the
16-byte payload with its own CRC16, now an end-to-end check layered on the
hardware CRC.

With two radios, the boot self-test transmits on A and receives on B **over the
air** — a genuine proof the FW2 self-test (same-pad loopback) could never be.

**Risk:** two antennas inches apart may saturate the receiver with near-field
rather than cleanly demodulating. Mitigation is the lowest
`cc1101_set_power` setting for the self-test. If it still saturates, the
self-test degrades to a two-board bench check and **reports that**, rather than
a false pass.

## Testing

Host tests (CTest, no SDK, no hardware). The existing 11 binaries keep covering
both boards, plus:

- `led_pattern` rendered at **both** n=7 and n=16.
- Beacon link framing: `0x40`/`0x41` round-trip, truncated and corrupt frames.
- Tone band-limiting: the ≥2.5 kHz clamp, and that no synthesized note
  overruns its buffer.

The SDL sim gains an OG geometry mode — 320×240, 7 LEDs, 5 buttons, no DVI
mirror window. It is selected by the same `-DWILIDORO_BOARD=fw2|og` configure
option as the device build, so one sim build is one board; there is no runtime
flag and no way for the two modes to be live at once.

## Risks, ranked

1. **The flash dance.** `fw flash wilidoro_display` takes the display CPU off
   USB. Most expensive mistake available; must be blocked in tooling.
2. **LIS3DH axis orientation** — bench-measured; blocks tilt-to-pause.
3. **LVGL heap sizing** — measure before building three themes on top of it.
4. **8 kHz chimes** may sound worse than FW2. Acceptable; retune if so.
5. **Near-field self-test saturation** — degrade honestly, never false-pass.

## Build order

1. Board target + build system; display app boots, LVGL up, one theme
2. Buttons + `FWOG_POWER_DEFAULT` power policy
3. LEDs (7)
4. Audio
5. Tilt (needs board)
6. Remaining two themes
7. Main-CPU app + beacon + over-the-air self-test

## Explicitly out of scope

Touch, DVI output, ambient auto-dim — no hardware. The OG's RTC, battery
gauge, IR, PDM mic and FPGA are unused; they are possible future work, not part
of this port.

## What Plan A settled, and what it left for the later plans

Plan A is implemented, reviewed and hardware-verified. Recorded here because
these were triaged during its final review and would otherwise be lost with the
scratch workspace.

**Resolved during Plan A, with the measured values the later plans inherit:**

- **RAM baseline: 152,188 B of 264 KB** (~58 %) for `wilidoro_display`, with
  LVGL's 64 KB heap and two 320×40 partial buffers. Three themes were budgeted
  against this; `LV_MEM_SIZE` is the knob if OG-C overruns it.
- **No RGB565 byte swap on the OG.** `st7789_blit()` converts to big-endian
  itself. Confirmed empirically — the digits render warm orange, not blue.
- **`hal_backlight()` takes percent; `board_backlight()` takes 0–255 duty.**
  The OG HAL scales. Anything that touches brightness must keep that straight.
- **Theme geometry is derived, not literal.** `src/ui/ui.h` carries the
  per-board block and `theme_neon.c` derives its arc from `UI_FACE_H`
  (`UI_FACE_H - 66`: 220 px on FW2, unchanged; 146 px on the OG, which fits).
  OG-C must follow this pattern rather than copying literals.

**Carried into Plan OG-B (LEDs, audio, tilt):**

- **Call `ws2812_init()`.** Until something does, `fwog_power_poll()` silently
  skips painting the red-hold shutdown countdown, because it only draws
  `if (ws2812_ready())`. The power-off itself works without it.
- **`hal_power_armed()` is already wired** into `app.c`'s LED write path and
  becomes load-bearing the moment the LEDs are real: it keeps the app off the
  bar while the BSP is drawing that countdown.
- **Harden `tools/flash_og.ps1` first.** `fw.py bootsel` samples the port list
  once, so the script cannot flash a board whose main app is resetting — which
  is exactly when you need it. It cost eight failed attempts during Plan A.
- The **LIS3DH axis orientation** remains a bench-measured constant and still
  blocks tilt-to-pause.

**Carried into Plan OG-C (themes, screens, simulator):**

- **Softkey label fit at 320×240 is unverified.** Buttons are
  `UI_SOFTKEY_BTN_W` = 60 px wide with a 16 px font; "Dismiss" is around 60 px
  and may clip. Check it when Settings and Nearby arrive.
- **`hal_caps()` has no consumer on the OG** — `screen_settings.c` is its only
  reader and is compiled out. Either OG-C builds a capability surface or this
  design should stop implying one exists.
