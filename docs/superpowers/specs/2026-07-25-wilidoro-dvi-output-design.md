# Wilidoro — DVI Big-Room Focus Display — Design

**Date:** 2026-07-25
**Status:** Approved design, pending implementation plan
**Target:** FreeWili 2 (RP2350B) · Pico SDK 2.x · LVGL 9 · wilibsp BSP
**Extends:** `2026-07-24-wilidoro-pomodoro-design.md` (adds a peripheral not in the
original spec)

## Overview

A second, independent display. The FreeWili 2's RP2350 HSTX block drives a
640×480p60 DVI output (GPIO 12–19); wilidoro uses it as a **room-readable focus
display**: a large state line, a huge `MM:SS` countdown, and session dots,
recolored by the active theme.

The 480×320 LCD remains the control surface and is not modified. The DVI output
always shows the timer, regardless of which screen the LCD is on — it is a room
display, not a mirror.

## Goals

- A countdown legible across a room, driven from the same timer state as the LCD.
- Live recoloring when the theme changes on the LCD.
- No regression to the audio verified in Plan C2, and no change to LCD rendering.
- The render logic pure and host-unit-tested, like `led_pattern` and `sound`.

## Non-goals

- **Per-theme DVI layouts.** One shared layout, three palettes. Three bespoke
  hand-drawn faces is roughly 3× the renderer work and 3× the tests; revisit
  once one layout is proven on hardware.
- **HDMI audio.** The source project's audio-island mode was not harvested into
  `wilibsp` — this DVI path carries video only.
- **Hotplug / EDID detection.** No DDC lines are wired; the app cannot know
  whether a monitor is attached.
- **Mirroring the LCD.** Rejected: it wastes the extra resolution, and the
  ST7796 uses byte-swapped RGB565 while HSTX wants native little-endian, so
  every pixel would need swapping.

## Clock decision

`hstx_dvi_init` sets `clk_hstx = clk_sys/2`, and the HSTX CSR divides by 5, so
the **pixel clock is `clk_sys/10`**.

**We stay at the board default 250 MHz**, giving 25.0 MHz — 0.7 % below the
640×480p60 standard 25.175 MHz, which `wilibsp/docs/drivers/dvi.md` records as
within most monitors' tolerance. This preserves Plan C2's audio exactly as
hardware-verified (NAU88C10 MCLK = `clk_sys/61`).

**Fallback if a monitor refuses to sync:** `board_init_clk(252000)` gives an
exact 25.2 MHz pixel clock at the cost of ~0.8 % audio pitch (≈14 cents —
acceptable for chimes). This fallback is only safe once the fs fix below lands.

### Required fix: derive the audio sample rate at runtime

`src/hal/hal_target.c` hardcodes `AUDIO_FS_HZ = 16009`, which is
`clk_sys/61/256` at 250 MHz. `tone_arm()` uses it to snap each tone to a **whole
number of sine cycles** in the 1024-frame ring buffer — that whole-cycle property
is what makes the DMA loop seam inaudible.

At 252 MHz the real fs becomes 16137 Hz. With the constant left at 16009 the
computed cycle count would no longer be whole, reintroducing a loop-seam click on
every tone. Derive fs from `clock_get_hz(clk_sys)` at runtime instead. This is
required regardless of whether we ever use the 252 MHz fallback: it removes a
landmine for any future clock change, and costs nothing.

## Architecture

```
src/app/
  dvi_view.h/.c    NEW   PURE host-tested: render the focus view into a
                         caller-supplied surface. No LVGL, no hal, no wilibsp.
                         Consumes timer_view_t + theme index.
src/hal/
  hal.h            MOD   + hal_dvi_surface(), hal_dvi_enable(), caps.dvi
  hal_target.c     MOD   hstx_dvi_init(480, 240); surface from
                         hstx_dvi_video_base()/_stride(); runtime AUDIO_FS_HZ
  hal_sim.c        MOD   surface backed by plain RAM, blitted into the SDL window
src/app/
  app.c            MOD   dirty-checked DVI redraw on the existing 200 ms tick
  app_model.h/.c   MOD   + bool dvi_on (Settings toggle, default true)
src/ui/
  screen_settings.c MOD  a "DVI output" row, matching the existing toggles
tests/
  test_dvi_view.c  NEW   stride-safety + layout + palette tests
```

Layering is unchanged: `dvi_view` never learns that HSTX exists, and only
`hal_target.c` touches wilibsp.

### The HAL seam

```c
typedef struct { uint16_t *base; int stride, w, h; } hal_dvi_surface_t;

/* False when DVI is unavailable (sim without a surface, or init failed). */
bool hal_dvi_surface(hal_dvi_surface_t *s);
void hal_dvi_enable(bool on);     /* blank/unblank the three data lanes */
```

`stride` is part of the interface, not an implementation detail: the DVI
framebuffer **interleaves HSTX command words between rows**, so rows are not
contiguous and `stride > w`. Row `y` starts at `base + y*stride` and is `w`
pixels wide.

Pixels are **native little-endian RGB565** — written directly, with no byte swap
(unlike the ST7796 path, which is big-endian).

### The renderer

```c
#define DVI_VIEW_W 480
#define DVI_VIEW_H 240

typedef struct { uint16_t *base; int stride, w, h; } dvi_surface_t;

/* Render the focus view for `theme_idx` (0=neon, 1=arcade, 2=flip; clamped)
   and the given timer state. Writes only x in [0,w) of each row. */
void dvi_view_render(uint8_t theme_idx, const timer_view_t *v,
                     const dvi_surface_t *s);

/* Caller-owned redraw tracking. Returns true when the visible output would
   differ from what this tracker last accepted, and updates it. */
typedef struct { uint32_t sig; bool valid; } dvi_dirty_t;
void dvi_dirty_reset(dvi_dirty_t *d);
bool dvi_view_dirty(dvi_dirty_t *d, uint8_t theme_idx, const timer_view_t *v);
```

The dirty tracker is **caller-owned state**, not a static inside the module —
the same choice `sound_player_t` makes. Hidden statics leak between unit tests
and make render-skipping untestable in isolation.

`dvi_surface_t` is `dvi_view`'s own type, mirroring `hal_dvi_surface_t`, so the
module keeps its zero-dependency property (the same reason `led_pattern` defines
its own `led_rgb_t`). `app.c` converts between them.

Layout, top to bottom in the 480×240 region:

| Element | Content |
|---|---|
| State line | `FOCUS` / `BREAK` / `PAUSED` / `READY` / `DONE!` |
| Countdown | `MM:SS`, scaled block digits, the dominant element |
| Session dots | filled/hollow dots, `X of N` |

Colors come from a per-theme palette keyed by theme index (0=neon, 1=arcade,
2=flip) — the same convention `led_pattern` uses, and likewise a convention
rather than an enforced link.

## Memory budget

The driver's buffer is `200 + H×(11 + W/2) + (480−H)×8` dwords. At 480×240 that
is 62,360 dwords = **244 KB**.

| | |
|---|---|
| Current BSS (post-C2) | 187 KB |
| DVI framebuffer | 244 KB |
| **Total** | **431 KB of 520 KB** |
| Headroom | ~89 KB |

That covers C3 (IMU — negligible) and C4 (radio buffers). If C4 needs more,
dropping LVGL's second draw buffer in `src/target/lvgl_port.c` reclaims a further
38 KB (480×40×2).

### Required upstream BSP change

`HSTX_VID_W_MAX` (480) and `HSTX_VID_H_MAX` (320) in
`wilibsp/bsp/display/hstx_dvi.h` are **unguarded** `#define`s, so an app cannot
size the framebuffer without editing the submodule — even though the header's own
comment anticipates exactly this ("lower this #define if the DVI driver is ever
linked alongside the USB/FatFs + LCD-strip stack in one binary").

Wrap both in `#ifndef` so an app can override them via a compile definition, then
bump the pin — the same fix-upstream-and-bump workflow used for the two Plan C2
audio fixes. At 320 rows the buffer is 320 KB, which with our 187 KB BSS would
leave ~13 KB for stack and heap and will not fit.

## Behavior

- **Always shows the timer**, independent of the LCD's current screen.
- **Redraw on the existing 200 ms app tick**, guarded by `dvi_view_dirty()` so a
  full repaint only happens when the second, state, or theme actually changes.
  The countdown ticks once per second, so 200 ms is ample.
- **Tearing** is accepted: scanout is continuous DMA and there is no second
  framebuffer to flip. For text that changes once a second this is not
  perceptible; double buffering would cost another 244 KB.
- **Settings toggle** `dvi_on` (default true) calls `hal_dvi_enable()`, matching
  the existing `beacon_on` / `focus_tick` toggles. The framebuffer is statically
  allocated either way — the toggle blanks the output, it does not reclaim RAM.

## Error handling

Consistent with the existing spec: each peripheral may fail independently and the
app always boots. `hal_dvi_surface()` returning false disables the feature and
sets `caps.dvi = false`; the app simply skips the redraw.

## Testing

**Host unit tests** (`tests/test_dvi_view.c`), designed around the one genuinely
dangerous failure mode:

- **Stride safety (the critical test).** Every test renders into a surface whose
  `stride` is deliberately wider than `w`, with the out-of-bounds columns
  pre-filled with a sentinel value; the test asserts the sentinel is untouched.
  On hardware those columns hold HSTX command words, so a write past `w` corrupts
  the scanout program and can wedge the display. This catches it in CTest instead
  of on a monitor.
- Digit rendering across a range of remaining times, including `MM:SS` rollover
  and the 00:00 boundary.
- Per-theme palettes differ, and an out-of-range theme index clamps to 0.
- State-line text for each `pm_state_t`, including paused and alarm.
- `dvi_view_dirty()` returns true on a freshly reset tracker, false for an
  unchanged second, and true across a second, state, or theme change — each
  exercised with its own `dvi_dirty_t`, proving no cross-test leakage.

**Simulator:** `hal_sim` backs the surface with ordinary RAM and blits it into
the SDL window, so the layout can be iterated on PC without flashing. Tuning a
visual layout through host tests and reflashes alone is impractical.

**On-hardware checklist** (user-gated, as always):

1. A monitor syncs at 25.0 MHz. If it does not, try the 252 MHz fallback and
   confirm audio still sounds correct with the runtime-fs fix in place.
2. The countdown is legible across a room; digits are not clipped.
3. Switching theme on the LCD recolors the DVI output live.
4. The Settings toggle blanks and restores the output.
5. Audio still plays correctly with DVI scanout running — the DVI DMA adds
   continuous memory-bus traffic alongside the audio TX DMA and the blocking
   ST7796 flush.
6. The LCD's refresh is not visibly degraded by that same contention.

## Risks

| Risk | Mitigation |
|---|---|
| Monitor rejects the 0.7 %-low pixel clock | 252 MHz fallback, enabled by the runtime-fs fix |
| DVI scanout DMA starves audio or LCD | Checklist items 5–6; scanout is zero-IRQ and does not touch `DMA_IRQ_0` |
| Renderer writes past `w` and corrupts command words | Sentinel stride tests on every render test |
| 89 KB headroom proves tight for C4 | Reclaim 38 KB from LVGL's second draw buffer |

## Decisions log

- Big-room focus display, not an LCD mirror — chosen by user.
- Own pure renderer rather than a second LVGL display or the BSP's `dvi_osd` —
  chosen by user; the interleaved command words make direct LVGL rendering
  fragile, and `font5x7` is not room-readable.
- 480×240 region (244 KB) over 480×320 (will not fit), 480×200, or 320×240 —
  chosen by user.
- Per-theme colors with one shared layout, not three bespoke layouts — chosen by
  user.
- Stay at 250 MHz and accept a 0.7 %-low pixel clock rather than perturb the
  hardware-verified audio — chosen by user.
- Simulator renders the DVI surface (accepted extra scope, to make the layout
  iterable without hardware) — chosen by user.
