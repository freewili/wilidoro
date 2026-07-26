# Wilidoro DVI Big-Room Focus Display — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Drive a 640×480p60 DVI monitor from the RP2350 HSTX block, showing a room-readable focus view — state word, huge `MM:SS` countdown, session dots — recolored by the active theme, with the LCD untouched as the control surface.

**Architecture:** A new pure, host-tested `src/app/dvi_view` renders the whole view into a caller-supplied **strided** RGB565 surface using only rectangle fills (7-segment digits and a 14-glyph 5×7 font), so it needs no LVGL, no font library and no wilibsp. A new HAL seam (`hal_dvi_surface` / `hal_dvi_enable`) hands it either the real HSTX framebuffer or, in the simulator, plain RAM that gets blitted into the SDL window. The app redraws it from the existing 200 ms tick, guarded by a caller-owned dirty tracker.

**Tech Stack:** C11, wilibsp (`hstx_dvi`), LVGL 9 (untouched by the renderer), SDL2 sim, CTest + `tests/greatest.h`.

## Global Constraints

- **HAL is the only hardware seam.** `dvi_view`, `app/`, `ui/`, `core/` never call wilibsp or SDL directly.
- **`dvi_view` is PURE**: includes only `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` and `timer_view.h`. No LVGL, no `hal.h`, no wilibsp. This is what makes it host-unit-testable.
- **`stride` may exceed `w`.** On the device the extra words per row are **HSTX scanout commands**. Writing into them corrupts the display program and can wedge scanout. Every write must be clipped to `x ∈ [0, w)` and `y ∈ [0, h)`.
- **Pixels are native little-endian RGB565** — written directly, **no byte swap** (unlike the ST7796 path, which is big-endian).
- **Stay at 250 MHz** (`board_init()`). Do NOT call `board_init_clk(252000)` in this plan. Pixel clock is `clk_sys/10` = 25.0 MHz.
- **Region is 480×240**, sized by compile definitions on the BSP target. A 480×320 region is 320 KB and will NOT fit alongside our 187 KB of BSS.
- **Theme index convention:** 0 = neon, 1 = arcade, 2 = flip — same key `led_pattern` and `theme.c` use. `dvi_view` does NOT include `theme.h`.
- **Do not change LCD rendering** (`src/target/lvgl_port.c`, `src/ui/screen_timer.c` face logic) or the audio behaviour verified in Plan C2, beyond the runtime-fs fix in Task 3.
- **Hardware access rule:** builds + host tests + sim run freely. Flashing/RTT/probe/camera require asking the user first.
- **Commit trailer on every commit:** `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

## Verification model

| What | How | Autonomous? |
|---|---|---|
| `dvi_view` geometry, palettes, dirty tracking, **stride safety** | host CTest (`tools/test.ps1`), suite goes 9 → 10 binaries | yes |
| BSP guards + sizing | device build links, no RAM overflow | yes |
| HAL + app wiring | device build zero warnings + host suite | yes |
| Layout looks right | **simulator** (Task 5) — this is why the sim renders it | yes |
| A monitor actually syncs at 25.0 MHz | flash + plug in a display | **NO — user-gated** |

---

## File Structure

```
wilibsp/bsp/display/hstx_dvi.h  MOD  #ifndef-guard HSTX_VID_W_MAX / _H_MAX  (upstream)
CMakeLists.txt                  MOD  size the BSP region; add dvi_view.c
src/app/
  dvi_view.h/.c        NEW   PURE host-tested renderer + dirty tracker
  app.c                MOD   dirty-checked DVI redraw on the 200 ms tick
  app_model.h/.c       MOD   + bool dvi_on (default true)
src/hal/
  hal.h                MOD   + hal_dvi_surface_t, hal_dvi_surface(), hal_dvi_enable(), caps.dvi
  hal_target.c         MOD   hstx_dvi_init(480,240); runtime audio fs; caps.dvi
  hal_sim.c            MOD   RAM-backed surface + SDL blit
src/ui/
  screen_settings.c    MOD   "DVI output" row
src/sim/
  main.c               MOD   create the DVI window
  CMakeLists.txt       MOD   add dvi_view.c
tests/
  test_dvi_view.c      NEW   stride sentinel, layout, palettes, dirty
  CMakeLists.txt       MOD   add test_dvi_view
docs/hardware-notes.md MOD   DVI section + user-gated checklist
```

---

### Task 1: Make the HSTX framebuffer size overridable (upstream BSP + pin bump)

**Files:**
- Modify: `wilibsp/bsp/display/hstx_dvi.h` (in the submodule clone at `wilidoro/wilibsp`)
- Modify: `CMakeLists.txt` (root)

**Interfaces:**
- Consumes: nothing.
- Produces: `HSTX_VID_W_MAX` / `HSTX_VID_H_MAX` become overridable via compile definitions; the wilidoro build sets them to 480 / 240, which shrinks `framebuf` from ~320 KB to ~244 KB.

**Why this is first:** `framebuf[HSTX_PLAIN_DWORDS]` is sized at compile time from those two macros. Passing `hstx_dvi_init(480, 240)` at runtime does **not** shrink it. Without this task the binary reserves 320 KB and will not fit.

- [ ] **Step 1: Guard the two size macros**

In `wilibsp/bsp/display/hstx_dvi.h`, replace the two `#define` lines (keeping the existing comments above them) so an app can override them:

```c
// Max stored DVI video width. Override with a compile definition (see below).
#ifndef HSTX_VID_W_MAX
#define HSTX_VID_W_MAX 480
#endif
// Max stored DVI video height. 480x320 = the full panel size and a ~327 KB SRAM
// command buffer (see hstx_dvi.c). Both maxima are compile-time because they size
// that buffer -- passing a smaller vid_h to hstx_dvi_init() does NOT shrink it.
// Lower them with a target compile definition when the DVI driver is linked
// alongside another RAM-hungry stack (LCD strip, USB/FatFs), e.g.
//   target_compile_definitions(freewili2_bsp PUBLIC HSTX_VID_H_MAX=240)
// Taller frames are center-cropped on DVI.
#ifndef HSTX_VID_H_MAX
#define HSTX_VID_H_MAX 320
#endif
```

- [ ] **Step 2: Size the region from wilidoro's build**

In the root `CMakeLists.txt`, immediately after the `add_subdirectory(wilibsp/bsp)` line, add:

```cmake
# Size the HSTX DVI framebuffer for wilidoro's 480x240 focus display. PUBLIC so the
# app and the BSP agree. The buffer is 200 + H*(11 + W/2) + (480-H)*8 dwords:
# 480x240 = 244 KB, which fits alongside our ~187 KB of BSS. The BSP default
# 480x320 is 320 KB and would leave ~5 KB for stack + heap.
target_compile_definitions(freewili2_bsp PUBLIC HSTX_VID_W_MAX=480 HSTX_VID_H_MAX=240)
```

- [ ] **Step 3: Verify the size actually changed**

Run: `powershell -File tools/build.ps1 -Clean`
Expected: PASS, zero warnings. Then confirm the reserved buffer shrank:

```bash
SDK=$(ls -d /c/Users/dave/.pico-sdk/toolchain/*/bin | tail -1)
"$SDK/arm-none-eabi-nm" --print-size --size-sort -r build/wilidoro.elf | head -5
```
Expected: a `framebuf` symbol of about `0x3cec0` (249,536 bytes ≈ 244 KB), **not** ~320 KB. If it is still ~320 KB the compile definition is not reaching the BSP target — do not proceed.

- [ ] **Step 4: Commit the BSP change, in the submodule**

```bash
cd wilibsp
git add bsp/display/hstx_dvi.h
git commit -m "feat(dvi): let apps size the HSTX framebuffer

HSTX_VID_W_MAX/HSTX_VID_H_MAX size framebuf[] at compile time, so passing a
smaller vid_h to hstx_dvi_init() does not shrink it -- an app linking DVI
alongside another RAM-hungry stack had no way to reclaim that SRAM without
editing this header. The header already advised lowering them; now that is
actually possible. #ifndef-guarded, so the 480x320 default is unchanged.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

**Do NOT push.** The controller handles pushing and the pin bump.

- [ ] **Step 5: Commit the wilidoro side**

```bash
cd <repo root>
git add CMakeLists.txt
git commit -m "build: size the HSTX DVI region to 480x240 (244 KB)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

> **Controller note (not the implementer's job):** after this task's review passes,
> push the `wilibsp` commit to `origin/master` and bump the submodule pin in
> wilidoro with a `fix(bsp):` commit — the same push-and-bump workflow used for the
> two Plan C2 audio fixes. Until the pin is bumped the build still works (it
> compiles the submodule working tree), but the repo does not record the dependency.

---

### Task 2: `dvi_view` — the pure renderer (TDD)

**Files:**
- Create: `src/app/dvi_view.h`, `src/app/dvi_view.c`, `tests/test_dvi_view.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `timer_view.h` (`timer_view_t` — fields `eff_state`, `idle`, `paused`, `alarm`, `break_phase`, `rem_ms`, `total_ms`, `session_idx`, `session_n`, `completed`, `streak`).
- Produces (this exact header — later tasks depend on these names verbatim):

```c
// src/app/dvi_view.h
#ifndef WILIDORO_DVI_VIEW_H
#define WILIDORO_DVI_VIEW_H
#include <stdint.h>
#include <stdbool.h>
#include "timer_view.h"

/* The stored DVI video region. Must match HSTX_VID_W_MAX/HSTX_VID_H_MAX in the
   build (see the root CMakeLists.txt) -- the layout constants assume this size. */
#define DVI_VIEW_W 480
#define DVI_VIEW_H 240

/* A strided, native-little-endian RGB565 surface. Row y starts at
   base + (size_t)y*stride and is w pixels wide.
   stride MAY EXCEED w: on the device the extra words per row are HSTX scanout
   COMMANDS. Writing into them corrupts the display program, so every write here
   is clipped to x in [0,w) and y in [0,h). */
typedef struct { uint16_t *base; int stride, w, h; } dvi_surface_t;

/* Render the whole view for `theme_idx` (0=neon, 1=arcade, 2=flip; out of range
   clamps to 0). Paints the background first, so no clearing is needed. */
void dvi_view_render(uint8_t theme_idx, const timer_view_t *v, const dvi_surface_t *s);

/* Caller-owned redraw tracking (NOT a static inside the module -- hidden state
   leaks between unit tests and makes render-skipping untestable). */
typedef struct { uint32_t sig; bool valid; } dvi_dirty_t;
void dvi_dirty_reset(dvi_dirty_t *d);
/* True when the visible output would differ from what `d` last accepted, and
   updates `d`. A freshly reset tracker always returns true. */
bool dvi_view_dirty(dvi_dirty_t *d, uint8_t theme_idx, const timer_view_t *v);
#endif
```

- [ ] **Step 1: Write the failing test**

Create `tests/test_dvi_view.c`:

```c
#include "greatest.h"
#include "dvi_view.h"
#include <string.h>

/* Surfaces are deliberately STRIDED: 120 uint16 of slack per row stands in for
   the HSTX command words that live between rows on real hardware. */
#define TSTRIDE (DVI_VIEW_W + 120)
#define SENTINEL 0xBEEF

static uint16_t g_buf[DVI_VIEW_H * TSTRIDE];
static uint16_t g_buf2[DVI_VIEW_H * TSTRIDE];

static dvi_surface_t surf(uint16_t *b) {
    for (int i = 0; i < DVI_VIEW_H * TSTRIDE; i++) b[i] = SENTINEL;
    dvi_surface_t s = { b, TSTRIDE, DVI_VIEW_W, DVI_VIEW_H };
    return s;
}
static uint16_t px(const uint16_t *b, int x, int y) { return b[(size_t)y * TSTRIDE + x]; }

/* Every out-of-bounds column of every row must still hold the sentinel. */
static int slack_intact(const uint16_t *b) {
    for (int y = 0; y < DVI_VIEW_H; y++)
        for (int x = DVI_VIEW_W; x < TSTRIDE; x++)
            if (px(b, x, y) != SENTINEL) return 0;
    return 1;
}

static timer_view_t mk(bool idle, bool paused, bool alarm, bool brk,
                       uint32_t rem_ms, unsigned idx, unsigned n) {
    timer_view_t v;
    memset(&v, 0, sizeof v);
    v.idle = idle; v.paused = paused; v.alarm = alarm; v.break_phase = brk;
    v.rem_ms = rem_ms; v.total_ms = 25u*60u*1000u;
    v.session_idx = idx; v.session_n = n;
    return v;
}

/* ---- THE critical test: never write into the command words ---- */
TEST render_never_writes_past_width(void) {
    dvi_surface_t s = surf(g_buf);
    timer_view_t v = mk(false, false, false, false, 25u*60u*1000u, 1, 4);
    dvi_view_render(0, &v, &s);
    ASSERT(slack_intact(g_buf));
    PASS();
}

TEST every_state_and_theme_stays_in_bounds(void) {
    timer_view_t states[5] = {
        mk(true,  false, false, false, 25u*60u*1000u, 0, 4),
        mk(false, false, false, false, 90u*1000u,     1, 4),
        mk(false, true,  false, false, 61u*1000u,     2, 4),
        mk(false, false, true,  false, 0,             3, 4),
        mk(false, false, false, true,  5u*60u*1000u,  4, 4),
    };
    for (int t = 0; t < 4; t++) {          /* includes out-of-range theme 3 */
        for (int i = 0; i < 5; i++) {
            dvi_surface_t s = surf(g_buf);
            dvi_view_render((uint8_t)t, &states[i], &s);
            ASSERT(slack_intact(g_buf));
        }
    }
    PASS();
}

TEST background_is_painted(void) {
    dvi_surface_t s = surf(g_buf);
    timer_view_t v = mk(false, false, false, false, 25u*60u*1000u, 1, 4);
    dvi_view_render(0, &v, &s);
    /* top-left corner is background, never a glyph */
    ASSERT(px(g_buf, 0, 0) != SENTINEL);
    ASSERT_EQ(px(g_buf, 0, 0), px(g_buf, DVI_VIEW_W - 1, 0));
    PASS();
}

/* 08:08 -- the tens-of-minutes digit is '0' (no middle segment), the units
   digit is '8' (has one). Middle segment sits at the digit's vertical centre. */
TEST seven_segment_digits_are_correct(void) {
    dvi_surface_t s = surf(g_buf);
    timer_view_t v = mk(false, false, false, false, (8u*60u + 8u) * 1000u, 1, 4);
    dvi_view_render(0, &v, &s);
    uint16_t bg = px(g_buf, 0, 0);
    const int y_mid = 68 + 120 / 2;          /* DIG_Y + DIG_H/2 */
    const int x_d0  = 36 + 84 / 2;           /* centre of digit 0 */
    const int x_d1  = 36 + 84 + 12 + 84 / 2; /* centre of digit 1 */
    ASSERT_EQ(bg, px(g_buf, x_d0, y_mid));   /* '0' has no middle bar */
    ASSERT(px(g_buf, x_d1, y_mid) != bg);    /* '8' does */
    PASS();
}

TEST themes_use_different_palettes(void) {
    timer_view_t v = mk(false, false, false, false, 25u*60u*1000u, 1, 4);
    dvi_surface_t a = surf(g_buf);  dvi_view_render(0, &v, &a);
    dvi_surface_t b = surf(g_buf2); dvi_view_render(1, &v, &b);
    ASSERT(memcmp(g_buf, g_buf2, sizeof g_buf) != 0);
    PASS();
}

TEST out_of_range_theme_clamps_to_zero(void) {
    timer_view_t v = mk(false, false, false, false, 25u*60u*1000u, 1, 4);
    dvi_surface_t a = surf(g_buf);  dvi_view_render(0, &v, &a);
    dvi_surface_t b = surf(g_buf2); dvi_view_render(9, &v, &b);
    ASSERT_EQ(0, memcmp(g_buf, g_buf2, sizeof g_buf));
    PASS();
}

TEST break_phase_recolors(void) {
    timer_view_t f = mk(false, false, false, false, 5u*60u*1000u, 1, 4);
    timer_view_t b = mk(false, false, false, true,  5u*60u*1000u, 1, 4);
    dvi_surface_t sa = surf(g_buf);  dvi_view_render(0, &f, &sa);
    dvi_surface_t sb = surf(g_buf2); dvi_view_render(0, &b, &sb);
    ASSERT(memcmp(g_buf, g_buf2, sizeof g_buf) != 0);
    PASS();
}

TEST state_word_changes_the_image(void) {
    timer_view_t i = mk(true,  false, false, false, 25u*60u*1000u, 0, 4);
    timer_view_t p = mk(false, true,  false, false, 25u*60u*1000u, 0, 4);
    dvi_surface_t sa = surf(g_buf);  dvi_view_render(0, &i, &sa);
    dvi_surface_t sb = surf(g_buf2); dvi_view_render(0, &p, &sb);
    ASSERT(memcmp(g_buf, g_buf2, sizeof g_buf) != 0);
    PASS();
}

/* ---- dirty tracking: each case gets its OWN tracker, proving no leakage ---- */
TEST dirty_is_true_on_a_fresh_tracker(void) {
    dvi_dirty_t d; dvi_dirty_reset(&d);
    timer_view_t v = mk(false, false, false, false, 60000, 1, 4);
    ASSERT(dvi_view_dirty(&d, 0, &v));
    PASS();
}

TEST dirty_is_false_within_the_same_second(void) {
    dvi_dirty_t d; dvi_dirty_reset(&d);
    timer_view_t v = mk(false, false, false, false, 60000, 1, 4);
    ASSERT(dvi_view_dirty(&d, 0, &v));
    timer_view_t v2 = mk(false, false, false, false, 59800, 1, 4);
    ASSERT_FALSE(dvi_view_dirty(&d, 0, &v2));   /* still 60 s when rounded up */
    PASS();
}

TEST dirty_is_true_across_a_second_theme_or_state(void) {
    timer_view_t v = mk(false, false, false, false, 60000, 1, 4);
    dvi_dirty_t a; dvi_dirty_reset(&a);
    ASSERT(dvi_view_dirty(&a, 0, &v));
    timer_view_t later = mk(false, false, false, false, 58000, 1, 4);
    ASSERT(dvi_view_dirty(&a, 0, &later));

    dvi_dirty_t b; dvi_dirty_reset(&b);
    ASSERT(dvi_view_dirty(&b, 0, &v));
    ASSERT(dvi_view_dirty(&b, 1, &v));          /* theme changed */

    dvi_dirty_t c; dvi_dirty_reset(&c);
    ASSERT(dvi_view_dirty(&c, 0, &v));
    timer_view_t paused = mk(false, true, false, false, 60000, 1, 4);
    ASSERT(dvi_view_dirty(&c, 0, &paused));     /* state changed */
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(render_never_writes_past_width);
    RUN_TEST(every_state_and_theme_stays_in_bounds);
    RUN_TEST(background_is_painted);
    RUN_TEST(seven_segment_digits_are_correct);
    RUN_TEST(themes_use_different_palettes);
    RUN_TEST(out_of_range_theme_clamps_to_zero);
    RUN_TEST(break_phase_recolors);
    RUN_TEST(state_word_changes_the_image);
    RUN_TEST(dirty_is_true_on_a_fresh_tracker);
    RUN_TEST(dirty_is_false_within_the_same_second);
    RUN_TEST(dirty_is_true_across_a_second_theme_or_state);
    GREATEST_MAIN_END();
}
```

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(test_dvi_view test_dvi_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/dvi_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/timer_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/pomodoro.c)
target_include_directories(test_dvi_view PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CORE} ${CMAKE_CURRENT_SOURCE_DIR}/../src/app)
target_compile_options(test_dvi_view PRIVATE -Wall -Wextra)
target_link_libraries(test_dvi_view m)
add_test(NAME test_dvi_view COMMAND test_dvi_view)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `powershell -File tools/test.ps1`
Expected: configure/build FAILS — `src/app/dvi_view.c` does not exist.

- [ ] **Step 3: Write `src/app/dvi_view.h`**

Create it verbatim from the **Interfaces → Produces** block above.

- [ ] **Step 4: Write `src/app/dvi_view.c`**

```c
// src/app/dvi_view.c -- the DVI big-room focus view. Pure: no LVGL, no hal, no
// wilibsp, so it is host-unit-tested.
//
// Everything is drawn from clipped rectangle fills: the countdown uses
// seven-segment digits and the state word a 14-glyph 5x7 font. That avoids
// pulling in a font library and keeps every write trivially bounded -- which
// matters because on the device the bytes past `w` in each row are HSTX scanout
// COMMANDS, not pixels. fill_rect() is the single choke point that enforces it.
#include "dvi_view.h"
#include <stddef.h>

/* Palette rows are keyed by theme index the same way led_pattern.c and theme.c
   are (0=neon, 1=arcade, 2=flip). That correspondence is convention, not
   enforced -- keep the three in step by hand. */
typedef struct { uint8_t r, g, b; } dvi_rgb_t;
typedef struct { dvi_rgb_t bg, focus, brk, dim; } dvi_palette_t;

static const dvi_palette_t PALETTES[3] = {
    /* neon   */ { {12,12,18},  {255,91,69},  {45,212,191},  {107,124,147} },
    /* arcade */ { {16,16,32},  {255,71,87},  {46,213,115},  {90,90,120}   },
    /* flip   */ { {28,24,20},  {200,120,60}, {138,125,107}, {92,84,74}    },
};

static inline uint16_t rgb565(dvi_rgb_t c) {
    return (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
}

/* The ONLY writer. Clips to the visible region so nothing can reach the HSTX
   command words that live past `w` in every row. */
static void fill_rect(const dvi_surface_t *s, int x, int y, int w, int h, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= s->w || y >= s->h) return;
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0) return;
    for (int r = 0; r < h; r++) {
        uint16_t *row = s->base + (size_t)(y + r) * (size_t)s->stride + x;
        for (int i = 0; i < w; i++) row[i] = c;
    }
}

/* Seven-segment: bit0=a(top) 1=b(upper right) 2=c(lower right) 3=d(bottom)
   4=e(lower left) 5=f(upper left) 6=g(middle). */
static const uint8_t SEG[10] = { 0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F };

static void draw_digit(const dvi_surface_t *s, int x, int y, int w, int h, int t,
                       int d, uint16_t c) {
    if (d < 0 || d > 9) return;
    uint8_t m = SEG[d];
    int hh = h / 2;
    if (m & 0x01) fill_rect(s, x + t,     y,             w - 2*t, t,      c);
    if (m & 0x02) fill_rect(s, x + w - t, y + t,         t,       hh - t, c);
    if (m & 0x04) fill_rect(s, x + w - t, y + hh,        t,       hh - t, c);
    if (m & 0x08) fill_rect(s, x + t,     y + h - t,     w - 2*t, t,      c);
    if (m & 0x10) fill_rect(s, x,         y + hh,        t,       hh - t, c);
    if (m & 0x20) fill_rect(s, x,         y + t,         t,       hh - t, c);
    if (m & 0x40) fill_rect(s, x + t,     y + hh - t/2,  w - 2*t, t,      c);
}

/* 5x7 glyphs, column-major, bit0 = top row. Only the letters the five state
   words need: FOCUS BREAK PAUSED READY DONE. */
static const char GLYPH_CH[] = "ABCDEFKNOPRSUY";
static const uint8_t GLYPH_COL[14][5] = {
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
};

static void draw_text(const dvi_surface_t *s, int x, int y, int scale,
                      const char *str, uint16_t c) {
    for (const char *p = str; *p; p++) {
        for (int i = 0; i < 14; i++) {
            if (GLYPH_CH[i] != *p) continue;
            for (int col = 0; col < 5; col++)
                for (int row = 0; row < 7; row++)
                    if (GLYPH_COL[i][col] & (1u << row))
                        fill_rect(s, x + col*scale, y + row*scale, scale, scale, c);
            break;
        }
        x += 6 * scale;   /* 5 columns + 1 blank */
    }
}

/* Layout for a 480x240 region. Countdown spans 36 + 408 = 444, leaving a
   symmetric 36 px margin either side. */
#define MARGIN_X    36
#define STATE_Y     18
#define STATE_SCALE  4
#define DIG_Y       68
#define DIG_W       84
#define DIG_H      120
#define DIG_T       16
#define DIG_GAP     12
#define COLON_W     24
#define DOT_Y      208
#define DOT_S       18
#define DOT_GAP     12

static const char *state_word(const timer_view_t *v) {
    if (v->alarm)  return "DONE";
    if (v->idle)   return "READY";
    if (v->paused) return "PAUSED";
    return v->break_phase ? "BREAK" : "FOCUS";
}

void dvi_view_render(uint8_t theme_idx, const timer_view_t *v, const dvi_surface_t *s) {
    const dvi_palette_t *p = &PALETTES[theme_idx < 3 ? theme_idx : 0];
    uint16_t bg  = rgb565(p->bg);
    uint16_t fg  = rgb565(v->break_phase ? p->brk : p->focus);
    uint16_t dim = rgb565(p->dim);

    fill_rect(s, 0, 0, s->w, s->h, bg);

    const char *word = state_word(v);
    int n_ch = 0; while (word[n_ch]) n_ch++;
    draw_text(s, (s->w - n_ch * 6 * STATE_SCALE) / 2, STATE_Y, STATE_SCALE, word, fg);

    /* Round UP so a running timer shows 25:00 on its first frame, not 24:59. */
    uint32_t secs = (v->rem_ms + 999u) / 1000u;
    unsigned mm = (unsigned)(secs / 60u), ss = (unsigned)(secs % 60u);
    if (mm > 99u) { mm = 99u; ss = 59u; }

    int x = MARGIN_X;
    draw_digit(s, x, DIG_Y, DIG_W, DIG_H, DIG_T, (int)(mm / 10u), fg); x += DIG_W + DIG_GAP;
    draw_digit(s, x, DIG_Y, DIG_W, DIG_H, DIG_T, (int)(mm % 10u), fg); x += DIG_W + DIG_GAP;
    fill_rect(s, x + COLON_W/2 - DIG_T/2, DIG_Y + DIG_H/3,     DIG_T, DIG_T, fg);
    fill_rect(s, x + COLON_W/2 - DIG_T/2, DIG_Y + 2*DIG_H/3,   DIG_T, DIG_T, fg);
    x += COLON_W + DIG_GAP;
    draw_digit(s, x, DIG_Y, DIG_W, DIG_H, DIG_T, (int)(ss / 10u), fg); x += DIG_W + DIG_GAP;
    draw_digit(s, x, DIG_Y, DIG_W, DIG_H, DIG_T, (int)(ss % 10u), fg);

    unsigned n = v->session_n ? v->session_n : 1u;
    if (n > 8u) n = 8u;
    int total_w = (int)n * DOT_S + ((int)n - 1) * DOT_GAP;
    int dx = (s->w - total_w) / 2;
    for (unsigned i = 0; i < n; i++)
        fill_rect(s, dx + (int)i * (DOT_S + DOT_GAP), DOT_Y, DOT_S, DOT_S,
                  i < v->session_idx ? fg : dim);
}

void dvi_dirty_reset(dvi_dirty_t *d) { d->sig = 0; d->valid = false; }

bool dvi_view_dirty(dvi_dirty_t *d, uint8_t theme_idx, const timer_view_t *v) {
    uint32_t secs = (v->rem_ms + 999u) / 1000u;
    if (secs > 0x3FFFFFu) secs = 0x3FFFFFu;
    uint32_t sig = secs
                 | ((uint32_t)(theme_idx & 3u)            << 22)
                 | ((uint32_t)(v->idle        ? 1u : 0u)  << 24)
                 | ((uint32_t)(v->paused      ? 1u : 0u)  << 25)
                 | ((uint32_t)(v->alarm       ? 1u : 0u)  << 26)
                 | ((uint32_t)(v->break_phase ? 1u : 0u)  << 27)
                 | ((uint32_t)(v->session_idx & 7u)       << 28);
    if (d->valid && d->sig == sig) return false;
    d->sig = sig; d->valid = true;
    return true;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `powershell -File tools/test.ps1`
Expected: **10/10 tests pass** (9 existing binaries plus `test_dvi_view`).

- [ ] **Step 6: Commit**

```bash
git add src/app/dvi_view.h src/app/dvi_view.c tests/test_dvi_view.c tests/CMakeLists.txt
git commit -m "feat(app): add the pure DVI focus-view renderer

Seven-segment countdown, 5x7 state word and session dots, drawn entirely from
clipped rectangle fills into a strided RGB565 surface. Every test renders into a
surface whose stride exceeds its width with the slack pre-filled with a
sentinel, because on hardware those bytes are HSTX scanout commands.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: Device HAL — real DVI surface + runtime audio sample rate

**Files:**
- Modify: `src/hal/hal.h`, `src/hal/hal_target.c`

**Interfaces:**
- Consumes: wilibsp via `fw2.h` — `hstx_dvi_init(int,int)`, `hstx_dvi_video_base(void) -> uint16_t*`, `hstx_dvi_video_stride(void) -> int`, `hstx_dvi_video_w(void) -> int`, `hstx_dvi_video_h(void) -> int`, `hstx_dvi_enable(bool)`. Also `clock_get_hz(clk_sys)` from `hardware/clocks.h`.
- Produces: in `src/hal/hal.h`:

```c
/* DVI big-room display. `stride` may exceed `w` -- see src/app/dvi_view.h. */
typedef struct { uint16_t *base; int stride, w, h; } hal_dvi_surface_t;
bool hal_dvi_surface(hal_dvi_surface_t *s);   /* false when DVI is unavailable */
void hal_dvi_enable(bool on);
```
plus a new `dvi` field in `hal_caps_t`.

- [ ] **Step 1: Extend `src/hal/hal.h`**

Add after the `hal_backlight` declaration:

```c
/* DVI big-room display (Plan DVI). The surface is a strided native-endian RGB565
   framebuffer: row y starts at base + (size_t)y*stride and is w pixels wide.
   stride MAY EXCEED w -- the slack holds HSTX scanout commands on the device.
   Geometry must match DVI_VIEW_W/DVI_VIEW_H in src/app/dvi_view.h and the
   HSTX_VID_*_MAX compile definitions in the root CMakeLists.txt. */
typedef struct { uint16_t *base; int stride, w, h; } hal_dvi_surface_t;
bool hal_dvi_surface(hal_dvi_surface_t *s);
void hal_dvi_enable(bool on);
```

And change the caps struct to:

```c
typedef struct { bool radio, imu, light, audio, buttons, leds, dvi; } hal_caps_t;
```

- [ ] **Step 2: Derive the audio sample rate at runtime (removes a latent Plan C2 trap)**

In `src/hal/hal_target.c`, add `#include "hardware/clocks.h"` to the includes.

Replace the `#define AUDIO_FS_HZ 16009` line with:

```c
/* The REAL I2S sample rate, derived at runtime from clk_sys (was hardcoded to
   the 250 MHz value, 16009). audio_i2s_duplex_init() picks an INTEGER MCLK PWM
   divider `ticks = clk_sys/(256*16000)`, so the codec actually runs at
   clk_sys/(ticks*256) -- 16009 Hz at 250 MHz, 16137 Hz at 252 MHz.
   tone_arm() snaps each tone to a WHOLE number of sine cycles against this
   value, and that whole-cycle property is the only reason the DMA loop seam is
   inaudible. Hardcoding it would reintroduce a per-loop click the moment the
   board clock changed -- e.g. the 252 MHz option for exact DVI pixel timing. */
static uint32_t s_audio_fs_hz = 16009u;
```

In `hal_init`, immediately before the `audio_i2s_duplex_init(16000);` call, insert:

```c
    {   /* mirror the driver's own integer-divider math */
        uint32_t sys = clock_get_hz(clk_sys);
        uint32_t ticks = sys / (256u * 16000u);
        if (ticks) s_audio_fs_hz = sys / (ticks * 256u);
        DIAG("audio: fs=%u Hz (clk_sys=%u kHz)\n",
             (unsigned)s_audio_fs_hz, (unsigned)(sys / 1000u));
    }
```

In `tone_arm`, replace the three `AUDIO_FS_HZ` uses with `s_audio_fs_hz`:

```c
    uint32_t cycles = ((uint32_t)hz * TONE_FRAMES + s_audio_fs_hz / 2u) / s_audio_fs_hz;
    if (cycles < 1u) cycles = 1u;
    if (cycles > TONE_FRAMES / 4u) cycles = TONE_FRAMES / 4u;   /* >=4 samples/period */
    float actual_hz = (float)cycles * (float)s_audio_fs_hz / (float)TONE_FRAMES;
```
and the `tone_gen_fill` call's sample-rate argument:
```c
    tone_gen_fill(mono, TONE_FRAMES, actual_hz, (float)s_audio_fs_hz, &phase);
```

- [ ] **Step 3: Bring up DVI and expose the surface**

Add near the other statics in `src/hal/hal_target.c`:

```c
/* ---------------- DVI --------------------------------------------------
 * 640x480p60 over the HSTX block (GPIO 12-19). The stored video region is
 * 480x240, sized by the HSTX_VID_*_MAX compile definitions in the root
 * CMakeLists.txt -- those size framebuf[] at COMPILE time, so the runtime
 * hstx_dvi_init() arguments alone would not shrink it.
 * Pixel clock is clk_sys/10 = 25.0 MHz at our 250 MHz board clock: 0.7 % below
 * the 25.175 MHz standard, but within most monitors' tolerance, and it keeps
 * the audio exactly as verified in Plan C2.
 * Scanout is a zero-IRQ DMA pair, so this registers no DMA_IRQ_0 handler. */
#define HAL_DVI_W 480
#define HAL_DVI_H 240
static bool s_dvi;
```

In `hal_init`, after the audio block, add:

```c
    hstx_dvi_init(HAL_DVI_W, HAL_DVI_H);
    s_dvi = (hstx_dvi_video_base() != NULL) &&
            hstx_dvi_video_w() == HAL_DVI_W && hstx_dvi_video_h() == HAL_DVI_H;
    DIAG("dvi: %s (%dx%d stride=%d)\n", s_dvi ? "ok" : "UNAVAILABLE",
         hstx_dvi_video_w(), hstx_dvi_video_h(), hstx_dvi_video_stride());
```

Add the two functions next to the other HAL implementations:

```c
bool hal_dvi_surface(hal_dvi_surface_t *s) {
    if (!s_dvi) return false;
    s->base   = hstx_dvi_video_base();
    s->stride = hstx_dvi_video_stride();
    s->w      = hstx_dvi_video_w();
    s->h      = hstx_dvi_video_h();
    return true;
}

void hal_dvi_enable(bool on) { if (s_dvi) hstx_dvi_enable(on); }
```

And add `.dvi = s_dvi` to the `hal_caps` initializer:

```c
hal_caps_t hal_caps(void) {
    hal_caps_t c = { .radio=false,.imu=false,.light=s_light,.audio=s_audio_ok,
                     .buttons=true,.leds=true,.dvi=s_dvi };
    return c;
}
```

- [ ] **Step 4: Build the device firmware**

Run: `powershell -File tools/build.ps1 -Clean`
Expected: PASS, **zero warnings**, no `region RAM overflowed`.

Then confirm the memory budget:
```bash
SDK=$(ls -d /c/Users/dave/.pico-sdk/toolchain/*/bin | tail -1)
"$SDK/arm-none-eabi-size" build/wilidoro.elf
```
Expected: `bss` around 440,000 bytes (≈187 KB before + ≈244 KB framebuffer). If it exceeds ~500,000 the region sizing from Task 1 is not in effect — stop and investigate rather than shrinking anything else.

- [ ] **Step 5: Run the host suite (regression gate)**

Run: `powershell -File tools/test.ps1`
Expected: 10/10 pass.

- [ ] **Step 6: Commit**

```bash
git add src/hal/hal.h src/hal/hal_target.c
git commit -m "feat(hal): DVI surface seam + runtime audio sample rate

Brings up 640x480p60 over HSTX with a 480x240 stored region and exposes it as a
strided RGB565 surface. Scanout is zero-IRQ, so no DMA_IRQ_0 handler is added.

Also derives the I2S sample rate from clk_sys instead of hardcoding the
250 MHz-derived 16009. tone_arm() snaps tones to whole sine cycles against it,
so a hardcoded value would reintroduce a loop-seam click on any clock change --
including the 252 MHz option for exact DVI pixel timing.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: Wire the DVI view into the app + a Settings toggle

**Files:**
- Modify: `src/app/app_model.h`, `src/app/app_model.c`, `src/app/app.c`, `src/ui/screen_settings.c`, `CMakeLists.txt`, `src/sim/CMakeLists.txt`

**Interfaces:**
- Consumes: `dvi_view.h` (Task 2) and `hal_dvi_surface()` / `hal_dvi_enable()` (Task 3).
- Produces: `app_settings_t.dvi_on`.

- [ ] **Step 1: Add `dvi_on` to the settings model**

In `src/app/app_model.h`, add a field to `app_settings_t` after `beacon_on`:

```c
    bool     dvi_on;       /* big-room DVI display */
```

In `src/app/app_model.c`, in `app_settings_defaults`, extend the second line:

```c
    s->volume = 70; s->focus_tick = false; s->beacon_on = true; s->theme = 0;
    s->dvi_on = true;
```

- [ ] **Step 2: Add `dvi_view.c` to both build targets**

In the root `CMakeLists.txt`, in the `add_executable(wilidoro ...)` source list, add after `src/app/sound.c`:

```cmake
    src/app/dvi_view.c
```

In `src/sim/CMakeLists.txt`, in the `add_executable(wilidoro_sim ...)` list, add after the `sound.c` line:

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/dvi_view.c
```

- [ ] **Step 3: Redraw the DVI view from the app tick**

In `src/app/app.c`, add `#include "dvi_view.h"` to the includes, and a static next to `s_alarm_next_ms`:

```c
static dvi_dirty_t s_dvi_dirty;
```

In `app_init`, next to the other resets, add:

```c
    dvi_dirty_reset(&s_dvi_dirty);
```

In `tick_cb`, immediately after the existing `timer_view_t lv = timer_view_make(...)` line (reuse that value — do not build a second one), add:

```c
    /* Big-room DVI display: repaint only when the visible content changes. The
       countdown ticks once a second, so the 200 ms cadence is ample. */
    hal_dvi_surface_t ds;
    if (s_app.settings.dvi_on && hal_dvi_surface(&ds)) {
        if (dvi_view_dirty(&s_dvi_dirty, s_app.settings.theme, &lv)) {
            dvi_surface_t vs = { ds.base, ds.stride, ds.w, ds.h };
            dvi_view_render(s_app.settings.theme, &lv, &vs);
        }
    }
```

- [ ] **Step 4: Apply the toggle**

Still in `src/app/app.c`, add a helper above `route_softkey`:

```c
/* Blank/unblank the DVI output and force a repaint next tick when re-enabled. */
void app_dvi_apply(void) {
    hal_dvi_enable(s_app.settings.dvi_on);
    if (s_app.settings.dvi_on) dvi_dirty_reset(&s_dvi_dirty);
}
```

Declare it in `src/app/app.h` next to `app_sound`:

```c
void   app_dvi_apply(void);          /* re-apply settings.dvi_on to the hardware */
```

Call it once at the end of `app_init`, after the timers are created:

```c
    app_dvi_apply();
```

- [ ] **Step 5: Add the Settings row**

In `src/ui/screen_settings.c`:

Add `s_val_dvi` to the statics on line 10:
```c
static lv_obj_t *s_val_focus, *s_val_short, *s_val_long, *s_val_vol, *s_val_beacon, *s_val_theme, *s_val_dvi;
```

Add to `refresh_values`, after the beacon line:
```c
    lv_label_set_text(s_val_dvi, s->dvi_on?"on":"off");
```

Extend the enum:
```c
enum { SET_FOCUS=1, SET_SHORT, SET_LONG, SET_VOL, SET_BEACON, SET_THEME, SET_DVI };
```

Add to the `adj_event` switch, after the `SET_THEME` case:
```c
        case SET_DVI:   s->dvi_on = !s->dvi_on; app_dvi_apply(); break;
```

Add the row after the theme row:
```c
    s_val_dvi    = add_row("DVI output",   SET_DVI);
```

- [ ] **Step 6: Verify**

Run, in order:
```
powershell -File tools/test.ps1
powershell -File tools/build.ps1 -Clean
```
Expected: host 10/10; device build clean, zero warnings, no RAM overflow.

The simulator will not build yet — `hal_sim.c` has no `hal_dvi_surface`/`hal_dvi_enable`. That is Task 5.

- [ ] **Step 7: Commit**

```bash
git add src/app/app_model.h src/app/app_model.c src/app/app.c src/app/app.h src/ui/screen_settings.c CMakeLists.txt src/sim/CMakeLists.txt
git commit -m "feat(app): drive the DVI focus view + a Settings toggle

Repaints from the existing 200 ms tick, gated by the dirty tracker so a full
repaint only happens when the second, state or theme actually changes.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 5: Simulator — render the DVI surface in its own window

**Files:**
- Modify: `src/hal/hal_sim.c`, `src/sim/main.c`

**Interfaces:**
- Consumes: `hal_dvi_surface_t` (Task 3), `DVI_VIEW_W`/`DVI_VIEW_H` (Task 2).
- Produces: `void sim_dvi_present(void);` in `src/hal/hal_sim.c`, called once per loop from `src/sim/main.c`.

**Why:** the whole point of DVI is a visual layout. Tuning it through host tests and reflashes alone is impractical; this makes it iterable on the PC. The surface is plain RAM here — the stride is deliberately made larger than the width so the simulator exercises the same strided path the device uses.

- [ ] **Step 1: Back the surface with RAM and blit it**

In `src/hal/hal_sim.c`, add `#include "dvi_view.h"` to the includes, and this block before `hal_caps`:

```c
/* --- DVI: plain RAM, blitted into a second SDL window ---------------------
   The stride is deliberately WIDER than the width so the simulator exercises
   the same strided path as the device, where the slack holds HSTX commands. */
#define SIM_DVI_STRIDE (DVI_VIEW_W + 64)
static uint16_t s_dvi_px[DVI_VIEW_H * SIM_DVI_STRIDE];
static SDL_Window   *s_dvi_win;
static SDL_Renderer *s_dvi_ren;
static SDL_Texture  *s_dvi_tex;
static bool s_dvi_on = true;

bool hal_dvi_surface(hal_dvi_surface_t *s) {
    s->base = s_dvi_px; s->stride = SIM_DVI_STRIDE;
    s->w = DVI_VIEW_W;  s->h = DVI_VIEW_H;
    return true;
}
void hal_dvi_enable(bool on) { s_dvi_on = on; }

void sim_dvi_create(void) {
    s_dvi_win = SDL_CreateWindow("wilidoro DVI (640x480 region 480x240)",
                                 SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                 DVI_VIEW_W, DVI_VIEW_H, SDL_WINDOW_SHOWN);
    if (!s_dvi_win) return;
    s_dvi_ren = SDL_CreateRenderer(s_dvi_win, -1, SDL_RENDERER_ACCELERATED);
    if (!s_dvi_ren) return;
    s_dvi_tex = SDL_CreateTexture(s_dvi_ren, SDL_PIXELFORMAT_RGB565,
                                  SDL_TEXTUREACCESS_STREAMING, DVI_VIEW_W, DVI_VIEW_H);
}

void sim_dvi_present(void) {
    if (!s_dvi_ren || !s_dvi_tex) return;
    if (s_dvi_on) {
        /* SDL takes a byte pitch; our stride is in uint16 elements. */
        SDL_UpdateTexture(s_dvi_tex, NULL, s_dvi_px, SIM_DVI_STRIDE * (int)sizeof(uint16_t));
    } else {
        SDL_SetRenderDrawColor(s_dvi_ren, 0, 0, 0, 255);
    }
    SDL_RenderClear(s_dvi_ren);
    if (s_dvi_on) SDL_RenderCopy(s_dvi_ren, s_dvi_tex, NULL, NULL);
    SDL_RenderPresent(s_dvi_ren);
}
```

Also set `.dvi=true` in the sim's `hal_caps` initializer:

```c
hal_caps_t hal_caps(void) { hal_caps_t c = { .radio=true,.imu=true,.light=true,.audio=true,.buttons=true,.leds=true,.dvi=true }; return c; }
```

- [ ] **Step 2: Create the window and present each loop**

In `src/sim/main.c`, declare the two helpers above `main` (they live in `hal_sim.c`; there is no sim-wide header):

```c
void sim_dvi_create(void);
void sim_dvi_present(void);
```

Then create the window after `lv_sdl_mouse_create();`:

```c
    sim_dvi_create();
```

and present inside the loop:

```c
    while (1) { hal_pump(); lv_timer_handler(); sim_dvi_present(); SDL_Delay(5); }
```

- [ ] **Step 3: Build and smoke-launch the simulator**

Run:
```
cmake -G Ninja -B build-sim -S src/sim -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe -DCMAKE_PREFIX_PATH=C:/msys64/mingw64
cmake --build build-sim
```
Expected: builds clean. Launch `build-sim/wilidoro_sim.exe` in the background, let it run a few seconds, confirm it does not crash, then terminate it. Two windows should appear: the 480×320 LCD and the 480×240 DVI view.

- [ ] **Step 4: Verify the other targets still pass**

Run:
```
powershell -File tools/test.ps1
powershell -File tools/build.ps1 -Clean
```
Expected: host 10/10; device build clean, zero warnings.

- [ ] **Step 5: Commit**

```bash
git add src/hal/hal_sim.c src/sim/main.c
git commit -m "feat(sim): render the DVI focus view in a second SDL window

Backs the surface with plain RAM at a stride wider than the width, so the
simulator exercises the same strided path as the HSTX framebuffer. Makes the
layout iterable without flashing.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 6: Document the DVI setup and its user-gated checklist

**Files:**
- Modify: `docs/hardware-notes.md`

**Interfaces:**
- Consumes: the constants from Tasks 1–4.
- Produces: nothing code-facing.

- [ ] **Step 1: Append a DVI section**

Insert this **before** the trailing `---` / "Why this note isn't in the BSP" block, so that closing note stays last:

```markdown
## DVI output — region size and pixel clock

The RP2350 HSTX block drives 640×480p60 DVI on GPIO 12–19. Wilidoro shows a
room-readable focus view there — state word, huge `MM:SS`, session dots — in the
active theme's colors. The LCD is unaffected and remains the control surface.

**Pixel clock is `clk_sys/10`.** At the board default 250 MHz that is 25.0 MHz —
0.7 % below the 25.175 MHz standard, but within most monitors' tolerance, and it
keeps the NAU88C10 audio exactly as verified. `board_init_clk(252000)` would give
an exact 25.2 MHz at the cost of ~0.8 % audio pitch; the sample rate is now
derived from `clk_sys` at runtime, so that switch is safe to make if a monitor
refuses to sync.

**The region is 480×240 = 244 KB**, set by
`target_compile_definitions(freewili2_bsp PUBLIC HSTX_VID_W_MAX=480 HSTX_VID_H_MAX=240)`
in the root `CMakeLists.txt`. Those macros size `framebuf[]` at **compile** time —
passing a smaller `vid_h` to `hstx_dvi_init()` does not shrink it. The BSP default
480×320 is 320 KB, which with our other ~187 KB of BSS leaves ~5 KB for stack and
heap and will not fit. (The `#ifndef` guards that make them overridable were added
upstream in `wilibsp`.)

**The framebuffer is strided**: rows are separated by HSTX scanout command words,
so row `y` starts at `hstx_dvi_video_base() + y*hstx_dvi_video_stride()` and is
only `hstx_dvi_video_w()` pixels wide. Writing past the width corrupts the scanout
program. `src/app/dvi_view.c` funnels every write through one clipped `fill_rect`,
and its unit tests render into a surface whose slack columns hold a sentinel value
to prove nothing escapes.

### On-device DVI checklist (pending — needs a flash session and a monitor)

1. A monitor syncs and shows the view at 25.0 MHz. If it does not, try
   `board_init_clk(252000)` and confirm the chimes still sound correct.
2. The countdown is legible across a room and no digits are clipped.
3. Switching theme on the LCD recolors the DVI output live.
4. The Settings "DVI output" toggle blanks and restores it.
5. Audio still plays correctly with DVI scanout running — the scanout DMA adds
   continuous memory-bus traffic alongside the audio TX DMA and the blocking
   ST7796 flush.
6. The LCD refresh is not visibly degraded by that same contention.
```

- [ ] **Step 2: Verify the whole branch**

Run, in order:
```
powershell -File tools/test.ps1
powershell -File tools/build.ps1 -Clean
cmake --build build-sim
```
Expected: 10/10 host tests; device build zero warnings, no overflow; sim builds.

- [ ] **Step 3: Commit**

```bash
git add docs/hardware-notes.md
git commit -m "docs: record the DVI region sizing, pixel clock and checklist

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Out of scope (do not do in this plan)

- Per-theme DVI **layouts** (colors only — three bespoke faces is a follow-up).
- HDMI audio (not harvested into wilibsp; this path is video-only).
- Hotplug / EDID detection (no DDC lines are wired).
- Mirroring the LCD, or any change to LVGL rendering or the ST7796 path.
- Double buffering the DVI framebuffer (another 244 KB; tearing on
  once-per-second text is not perceptible).
- Switching the board to 252 MHz. The runtime-fs fix makes it *safe*, but the
  decision needs the on-device checklist first.
- Plan C3 (IMU gestures) and C4 (CC1101 beacon).
