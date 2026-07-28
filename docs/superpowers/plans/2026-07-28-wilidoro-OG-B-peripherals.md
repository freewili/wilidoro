# Wilidoro OG Plan B — Peripherals — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn on the three peripherals Plan A stubbed — the 7-LED progress ring, the synthesized chimes, and tilt-to-pause — so the OG pomodoro uses the whole board.

**Architecture:** Every change lands behind the existing `src/hal/hal.h` seam, in `src/hal/hal_og.c`. Nothing in `src/core/`, `src/app/` or `src/ui/` changes except one new pure, host-tested unit (`src/app/tone_synth.c`) that renders note samples, because the OG's I²S driver takes sample buffers where the FreeWili 2's codec took a frequency.

**Tech Stack:** C11, Pico SDK 2.3.0 (RP2040), `wiliOGbsp` BSP, CMake + Ninja, CTest + greatest.h.

**Spec:** `docs/superpowers/specs/2026-07-28-wilidoro-og-port-design.md`
**Predecessor:** `docs/superpowers/plans/2026-07-28-wilidoro-OG-A-foundation.md` (complete, hardware-verified)

## Global Constraints

Every task's requirements implicitly include this section.

- **Target:** FreeWili OG display CPU, RP2040 @ 200 MHz. `PICO_BOARD` is set in CMake only — **never** on a command line. Build with `powershell -File tools/build_og.ps1`.
- **Flashing is always `powershell -File tools/flash_og.ps1`**, which flashes only `wilidoro_main`; it carries the display image over the inter-CPU link. **Never flash a display app by UF2** — the display CPU has no BOOTSEL button.
- **`hal_pump()` must remain the single `fwog_power_poll()` call site, once per main-loop iteration.** A second poll consumes button edges out from under the ship-mode hold machine.
- **Never add a watchdog to the display CPU.** Do not touch `src/main_og/main.c` (main CPU; it kicks its own 2 s watchdog as its loop's first statement and must keep doing so).
- **Keep the bounded ST7789 init** in `src/target_og/main.c` before `lvgl_port_og_init()`. Removing it or unbounding the wait puts the bootloader's UI back on the panel with no console signal.
- **No `printf`** — `DIAG()` only. UART0 is the inter-CPU link on both CPUs.
- **Never hardcode a PIO divider or a baud rate**; derive from `clock_get_hz()`.
- **PIO allocation is fixed by the BSP's own record:** `pio0` sm0 = WS2812, sm1 = PDM (unused here), sm2 = I²S; `pio1` = IR (unused here). Use `ws2812_init(pio0, 0)` and `i2s_audio_init(pio0, 2)`.
- **`hal_caps()` must stay honest.** Flip `leds`, `audio`, `imu` to `true` only in the task that actually makes each work, never ahead of it.
- **The FreeWili 2 build must not regress** and the **11 host test binaries must stay green** (this plan adds a 12th).
- **`wiliOGbsp/` is a read-only submodule.** Never modify it.
- **Commit discipline:** every task ends on a green build/test and a commit. Trailer:
  `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`

## Hardware facts this plan depends on

Measured or read from source during Plan A and the research for this one. Do not re-derive them; do not "correct" them without a board.

- **The OG WS2812 driver has no brightness control.** The FreeWili 2's does (`ws2812_set_brightness()`), the OG's does not — its API is `ws2812_init/set_color/get_color/process/ready` only. `hal_led_brightness()` must therefore scale in software before calling `ws2812_set_color()`.
- **`ws2812_process()` is blocking and DMA-free**; it pushes every word with `pio_sm_put_blocking()`. Call it once after setting pixels, not in a tight loop.
- **`fwog_power_poll()` paints the red-hold shutdown countdown on this same chain**, but only `if (ws2812_ready())`. Once Task 1 calls `ws2812_init()`, that countdown becomes visible for the first time — and `hal_power_armed()`, already wired into `src/app/app.c`'s LED block by Plan A, starts doing real work by keeping the app off the bar during it.
- **I²S runs at 8000 Hz, 16-bit**, fixed (`i2s_audio.c` derives its divider from `clock_get_hz(clk_sys)` against 8000). Nyquist is 4 kHz while `sound.h`'s `SOUND_HZ_MAX` is 3000, so the top of the shared note range must be clamped in the OG HAL — **not** in `sound.c`, whose tables are shared with the FreeWili 2.
- **`i2s_audio_start(samples, count, force_mono, is_8bit)`** takes a caller-owned buffer; `i2s_audio_process()` must be called regularly to feed its A/B chain; `i2s_audio_is_idle()` reports completion.
- **The LIS3DH is the OG's accelerometer** (the FreeWili 2 has a BMI323). API: `lis3dh_init()`, `lis3dh_configure(lis3dh_range_t)`, `lis3dh_process(int32_t move_threshold, lis3dh_sample_t *out_sample, lis3dh_motion_t *out_motion)`, where `lis3dh_sample_t` is `{int16_t x, y, z}`.
- **Which axis reads "flat" is a bench-measured constant** and is not derivable from the datasheet. Task 4 measures it on a board; it cannot be guessed.

---

## File Structure

```
src/app/
  tone_synth.h  tone_synth.c    NEW  pure note -> 16-bit PCM renderer, host-tested
src/hal/
  hal_og.c                      MODIFIED  LEDs, audio, IMU replace their stubs
src/target_og/
  main.c                        MODIFIED  peripheral bring-up + DIAG of what came up
tests/
  test_tone_synth.c             NEW  12th host test binary
  CMakeLists.txt                MODIFIED  register it
cmake/board_og.cmake            MODIFIED  add tone_synth.c to wilidoro_display
docs/hardware-notes.md          MODIFIED  the bench-set LED ceiling and IMU axis
```

---

### Task 1: The 7-LED progress ring

Camera-verifiable end to end: a running session should light LEDs proportionally.

**Files:**
- Modify: `src/hal/hal_og.c`
- Modify: `src/target_og/main.c`
- Modify: `docs/hardware-notes.md`

**Interfaces:**
- Consumes: `hal_led_count()` (returns `FWOG_LED_COUNT`, 7); `led_pattern_render(theme, view, out, count)` from Plan A.
- Produces: working `hal_led_set/brightness/show`; `hal_caps().leds == true`.

- [ ] **Step 1: Bring the chain up**

In `src/target_og/main.c`, after the ST7789 init block and before `lvgl_port_og_init()`, add:

```c
    /* pio0 sm0 -- the BSP's documented allocation (sm1 PDM, sm2 I2S). This is
       also what makes fwog_power_poll()'s red-hold countdown visible: it only
       paints the bar if ws2812_ready(). */
    const bool leds_ok = ws2812_init(pio0, 0);
    if (!leds_ok) DIAG("[wilidoro] ws2812 init FAILED\n");
```

Add `#include "hardware/pio.h"` if it is not already present. Extend the 1 Hz heartbeat to carry this too, e.g. `alive (panel=%s leds=%s)` — the same reasoning as Plan A's panel status: a boot-time `DIAG` is emitted before USB CDC enumerates and is dropped.

- [ ] **Step 2: Implement the three LED entry points**

Replace the stubs in `src/hal/hal_og.c`. The software brightness scale is the part that differs from the FreeWili 2:

```c
/* The OG's WS2812 driver has no brightness control -- the FreeWili 2's
   ws2812_set_brightness() has no counterpart here -- so scale in software on
   the way in and keep the chain's own values at what we want lit. */
static uint8_t s_led_bright = 255;
static uint8_t s_led_rgb[FWOG_LED_COUNT][3];

int hal_led_count(void) { return FWOG_LED_COUNT; }

void hal_led_set(int i, uint8_t r, uint8_t g, uint8_t b) {
    if (i < 0 || i >= FWOG_LED_COUNT) return;
    s_led_rgb[i][0] = r; s_led_rgb[i][1] = g; s_led_rgb[i][2] = b;
}

void hal_led_brightness(uint8_t level) { s_led_bright = level; }

void hal_led_show(void) {
    if (!ws2812_ready()) return;
    for (unsigned i = 0; i < FWOG_LED_COUNT; i++) {
        ws2812_set_color(i,
            (uint8_t)(((unsigned)s_led_rgb[i][0] * s_led_bright) / 255u),
            (uint8_t)(((unsigned)s_led_rgb[i][1] * s_led_bright) / 255u),
            (uint8_t)(((unsigned)s_led_rgb[i][2] * s_led_bright) / 255u));
    }
    ws2812_process();   /* blocking, DMA-free: once per show, never in a spin */
}
```

- [ ] **Step 3: Report the capability honestly**

In `hal_caps()`, change `c.leds = false; /* Plan OG-B */` to `c.leds = true;`.

- [ ] **Step 4: Build and run the host tests**

Run: `powershell -File tools/build_og.ps1`
Then: `powershell -File tools/test.ps1`
Expected: OG images build; 11/11 host tests still pass (this task adds none).

- [ ] **Step 5: Verify the FreeWili 2 is unaffected**

Run: `powershell -File tools/build.ps1`
Expected: green. This task touches only `hal_og.c` and `src/target_og/`, neither of which is in the FW2 target — confirm that is still true.

- [ ] **Step 6: Flash and verify on hardware**

Run: `powershell -File tools/flash_og.ps1`

Read the display console (`fw.py console` exits immediately when non-interactive; use a PowerShell `SerialPort` read on COM59 instead) and confirm `leds=ok` in the heartbeat.

Then, **with a human or a camera on the board**:
- Idle: all 7 LEDs dark.
- Press grey to start a focus session: LEDs light progressively as the session burns down — 1 lit at ~1/7 elapsed, 3 at half, 7 near the end. The colour is the neon theme's warm focus colour.
- The app's LED brightness is scaled by `LED_BRIGHT_MAX` in `src/app/app.c` (40/255 on the FreeWili 2). If the OG's chain is visibly too bright or too dim at that value, record the bench-chosen number rather than guessing — see Step 7.
- Hold red ~6 s: the BSP's shutdown countdown should now be **visible** on the bar (it was invisible throughout Plan A because nothing called `ws2812_init()`), and the app's own LED writes must not fight it — `src/app/app.c` already guards them with `hal_power_armed()`.

- [ ] **Step 7: Record the bench-set brightness in the hardware notes**

Add to the FreeWili OG section of `docs/hardware-notes.md` the LED ceiling actually used on this board and one sentence on how it was chosen (looked at, on the bench, at what ambient light). If `LED_BRIGHT_MAX` needed a per-board value, say so and state both.

- [ ] **Step 8: Commit**

```bash
git add src/hal/hal_og.c src/target_og/main.c docs/hardware-notes.md
git commit -m "feat(og): drive the 7-LED progress ring

The OG's WS2812 driver has no brightness control, so hal_led_brightness()
scales in software on the way to ws2812_set_color(). Bringing the chain up
also makes the BSP's red-hold shutdown countdown visible for the first
time, which is what hal_power_armed()'s guard in app.c exists for."
```

---

### Task 2: A pure, host-tested tone renderer

The FreeWili 2's HAL takes a frequency and hands it to a codec. The OG's I²S driver takes **sample buffers**, so something has to synthesize them — and the spec requires that something be host-tested.

**Files:**
- Create: `src/app/tone_synth.h`, `src/app/tone_synth.c`
- Create: `tests/test_tone_synth.c`
- Modify: `tests/CMakeLists.txt`
- Modify: `cmake/board_og.cmake`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `#define TONE_RATE_HZ 8000u`, `#define TONE_MAX_MS 400u`, `#define TONE_MAX_SAMPLES 3200u`, `#define TONE_HZ_CEILING 2500u`, `#define TONE_PEAK_MAX 12000`
  - `uint16_t tone_clamp_hz(uint16_t hz);`
  - `size_t tone_render(uint16_t hz, uint16_t ms, uint8_t amp, int16_t *out, size_t max);`
  Task 3 calls both from `hal_og.c`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_tone_synth.c`:

```c
#include "greatest.h"
#include "tone_synth.h"
#include <stdlib.h>

TEST clamps_above_ceiling(void) {
    ASSERT_EQ(TONE_HZ_CEILING, tone_clamp_hz(3000));
    ASSERT_EQ(TONE_HZ_CEILING, tone_clamp_hz(TONE_HZ_CEILING + 1));
    PASS();
}

TEST passes_through_below_ceiling(void) {
    ASSERT_EQ(440u,  tone_clamp_hz(440));
    ASSERT_EQ(TONE_HZ_CEILING, tone_clamp_hz(TONE_HZ_CEILING));
    PASS();
}

TEST rest_is_zero_hz_and_stays_zero(void) {
    ASSERT_EQ(0u, tone_clamp_hz(0));   /* a rest must not be clamped up */
    PASS();
}

TEST sample_count_follows_duration(void) {
    int16_t buf[TONE_MAX_SAMPLES];
    /* 8000 Hz * 40 ms = 320 samples */
    ASSERT_EQ(320u, tone_render(440, 40, 200, buf, TONE_MAX_SAMPLES));
    /* the documented maximum note */
    ASSERT_EQ(TONE_MAX_SAMPLES, tone_render(440, TONE_MAX_MS, 200, buf, TONE_MAX_SAMPLES));
    PASS();
}

TEST never_exceeds_the_buffer(void) {
    int16_t buf[64];
    /* Ask for far more than fits; it must truncate to `max`, not overrun. */
    const size_t n = tone_render(440, TONE_MAX_MS, 200, buf, 64);
    ASSERT(n <= 64u);
    PASS();
}

TEST does_not_write_past_what_it_returns(void) {
    enum { CAP = 512 };
    int16_t buf[CAP];
    for (int i = 0; i < CAP; i++) buf[i] = 0x5A5A;      /* sentinel */
    const size_t n = tone_render(440, 40, 200, buf, CAP);   /* 320 samples */
    for (size_t i = n; i < (size_t)CAP; i++) ASSERT_EQ((int16_t)0x5A5A, buf[i]);
    PASS();
}

TEST rest_renders_silence(void) {
    int16_t buf[TONE_MAX_SAMPLES];
    const size_t n = tone_render(0, 40, 200, buf, TONE_MAX_SAMPLES);
    ASSERT_EQ(320u, n);                       /* a rest still occupies time */
    for (size_t i = 0; i < n; i++) ASSERT_EQ(0, buf[i]);
    PASS();
}

TEST zero_amplitude_renders_silence(void) {
    int16_t buf[TONE_MAX_SAMPLES];
    const size_t n = tone_render(440, 40, 0, buf, TONE_MAX_SAMPLES);
    for (size_t i = 0; i < n; i++) ASSERT_EQ(0, buf[i]);
    PASS();
}

TEST louder_amplitude_gives_a_bigger_peak(void) {
    int16_t a[TONE_MAX_SAMPLES], b[TONE_MAX_SAMPLES];
    const size_t na = tone_render(440, 40, 80,  a, TONE_MAX_SAMPLES);
    const size_t nb = tone_render(440, 40, 240, b, TONE_MAX_SAMPLES);
    int pa = 0, pb = 0;
    for (size_t i = 0; i < na; i++) { const int v = abs(a[i]); if (v > pa) pa = v; }
    for (size_t i = 0; i < nb; i++) { const int v = abs(b[i]); if (v > pb) pb = v; }
    ASSERT(pb > pa);
    PASS();
}

TEST peak_respects_the_speaker_ceiling(void) {
    int16_t buf[TONE_MAX_SAMPLES];
    const size_t n = tone_render(440, 40, 255, buf, TONE_MAX_SAMPLES);
    int peak = 0;
    for (size_t i = 0; i < n; i++) { const int v = abs(buf[i]); if (v > peak) peak = v; }
    ASSERT(peak <= TONE_PEAK_MAX);
    ASSERT(peak > TONE_PEAK_MAX / 2);   /* full amplitude should approach it */
    PASS();
}

TEST zero_duration_renders_nothing(void) {
    int16_t buf[TONE_MAX_SAMPLES];
    ASSERT_EQ(0u, tone_render(440, 0, 200, buf, TONE_MAX_SAMPLES));
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(clamps_above_ceiling);
    RUN_TEST(passes_through_below_ceiling);
    RUN_TEST(rest_is_zero_hz_and_stays_zero);
    RUN_TEST(sample_count_follows_duration);
    RUN_TEST(never_exceeds_the_buffer);
    RUN_TEST(does_not_write_past_what_it_returns);
    RUN_TEST(rest_renders_silence);
    RUN_TEST(zero_amplitude_renders_silence);
    RUN_TEST(louder_amplitude_gives_a_bigger_peak);
    RUN_TEST(peak_respects_the_speaker_ceiling);
    RUN_TEST(zero_duration_renders_nothing);
    GREATEST_MAIN_END();
}
```

- [ ] **Step 2: Register the test binary**

Append to `tests/CMakeLists.txt`, following the shape of the existing `add_executable` blocks for app-layer units:

```cmake
add_executable(test_tone_synth test_tone_synth.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/tone_synth.c)
target_include_directories(test_tone_synth PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../src/app)
target_compile_options(test_tone_synth PRIVATE -Wall -Wextra)
target_link_libraries(test_tone_synth m)
add_test(NAME test_tone_synth COMMAND test_tone_synth)
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `powershell -File tools/test.ps1`
Expected: FAIL — `tone_synth.h` does not exist.

- [ ] **Step 4: Write the header**

Create `src/app/tone_synth.h`:

```c
/* Pure note -> 16-bit PCM renderer for the FreeWili OG's I2S output.
 *
 * The FreeWili 2's HAL hands a frequency to a codec; the OG's i2s_audio driver
 * takes sample buffers, so the samples have to come from somewhere. Keeping
 * that somewhere pure is what lets the band-limiting and the buffer bounds be
 * tested on a host with no board.
 *
 * No SDK, no hal, no LVGL: standard C only. */
#ifndef WILIDORO_TONE_SYNTH_H
#define WILIDORO_TONE_SYNTH_H
#include <stddef.h>
#include <stdint.h>

/* The OG's I2S block is fixed at 8 kHz, 16-bit (i2s_audio.c derives its PIO
   divider from clock_get_hz(clk_sys) against this rate). */
#define TONE_RATE_HZ     8000u

/* sound.h caps a note at 400 ms, which is 3200 samples here. */
#define TONE_MAX_MS      400u
#define TONE_MAX_SAMPLES ((TONE_RATE_HZ * TONE_MAX_MS) / 1000u)   /* 3200 */

/* Nyquist is 4 kHz. sound.h's shared note tables go to SOUND_HZ_MAX 3000,
   which alias badly here, so the OG clamps -- in the HAL rather than in
   sound.c, whose tables are shared with the FreeWili 2. */
#define TONE_HZ_CEILING  2500u

/* Speaker-safe peak. The FreeWili 2 caps amplitude in its codec path; this is
   the OG's equivalent, applied at synthesis. */
#define TONE_PEAK_MAX    12000

/* hz == 0 is a rest and passes through unclamped. */
uint16_t tone_clamp_hz(uint16_t hz);

/* Render a `ms`-long tone at `hz` with nominal loudness `amp` (0..255) into
   `out`, writing at most `max` samples and returning how many were written.
   hz == 0 renders silence of the same duration. Never writes past what it
   returns, and never past `max`. */
size_t tone_render(uint16_t hz, uint16_t ms, uint8_t amp, int16_t *out, size_t max);
#endif
```

- [ ] **Step 5: Write the implementation**

Create `src/app/tone_synth.c`:

```c
#include "tone_synth.h"
#include <math.h>

uint16_t tone_clamp_hz(uint16_t hz) {
    if (hz == 0u) return 0u;                       /* a rest stays a rest */
    return hz > TONE_HZ_CEILING ? (uint16_t)TONE_HZ_CEILING : hz;
}

size_t tone_render(uint16_t hz, uint16_t ms, uint8_t amp, int16_t *out, size_t max) {
    if (!out || ms == 0u) return 0u;

    size_t n = ((size_t)TONE_RATE_HZ * (size_t)ms) / 1000u;
    if (n > max) n = max;

    hz = tone_clamp_hz(hz);
    if (hz == 0u || amp == 0u) {                   /* rest, or silent */
        for (size_t i = 0; i < n; i++) out[i] = 0;
        return n;
    }

    /* A sine, not a square: at 8 kHz a square's harmonics fold straight back
       into the audible band. Float, not double -- the RP2040 has no FPU and
       doubles are markedly slower in soft-float. ~3200 sinf calls for the
       longest note is a couple of milliseconds, once per note, before that
       note starts playing. */
    const float peak = ((float)amp / 255.0f) * (float)TONE_PEAK_MAX;
    const float step = 2.0f * 3.14159265f * (float)hz / (float)TONE_RATE_HZ;
    for (size_t i = 0; i < n; i++) {
        out[i] = (int16_t)(peak * sinf(step * (float)i));
    }
    return n;
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `powershell -File tools/test.ps1`
Expected: PASS — 12/12 binaries green, `test_tone_synth` running 11 tests.

- [ ] **Step 7: Add the source to the OG target**

In `cmake/board_og.cmake`, add `src/app/tone_synth.c` to `wilidoro_display`'s source list. Do **not** add it to `cmake/board_fw2.cmake` — the FreeWili 2 does not use it.

Run: `powershell -File tools/build_og.ps1` and `powershell -File tools/build.ps1`
Expected: both green.

- [ ] **Step 8: Commit**

```bash
git add src/app/tone_synth.h src/app/tone_synth.c tests/test_tone_synth.c \
        tests/CMakeLists.txt cmake/board_og.cmake
git commit -m "feat(og): pure, host-tested tone renderer for the 8 kHz I2S path

The OG's i2s_audio driver takes sample buffers where the FreeWili 2's codec
took a frequency. Keeping the synthesis pure is what lets the 2.5 kHz
band-limit and the buffer bounds be tested without a board -- Nyquist here
is 4 kHz, while the shared note tables in sound.h reach 3000."
```

---

### Task 3: Wire the renderer to the I²S block

**Files:**
- Modify: `src/hal/hal_og.c`
- Modify: `src/target_og/main.c`

**Interfaces:**
- Consumes: `tone_clamp_hz()`, `tone_render()`, `TONE_MAX_SAMPLES` (Task 2).
- Produces: working `hal_tone()`/`hal_audio_idle()`; `hal_caps().audio == true`.

- [ ] **Step 1: Bring the I²S block up**

In `src/target_og/main.c`, next to the WS2812 init from Task 1:

```c
    /* pio0 sm2 -- the BSP's documented allocation, sharing the block with
       WS2812 on sm0. */
    const bool audio_ok = i2s_audio_init(pio0, 2);
    if (!audio_ok) DIAG("[wilidoro] i2s init FAILED\n");
```

Extend the 1 Hz heartbeat again so it reads e.g. `alive (panel=%s leds=%s audio=%s)`.

- [ ] **Step 2: Implement the audio entry points**

In `src/hal/hal_og.c`, replace the audio stubs. Add `#include "tone_synth.h"` at the top.

```c
/* One note at a time. i2s_audio_start() takes a caller-owned buffer and reads
   from it while the note plays, so this must stay alive until the note ends --
   hence static, not a stack array. 3200 int16 is 6.4 KB. */
static int16_t s_tone_buf[TONE_MAX_SAMPLES];

void hal_tone(uint16_t hz, uint16_t ms, uint8_t amp) {
    if (ms == 0u) return;
    i2s_audio_stop();                       /* always stop before re-arming */
    const size_t n = tone_render(hz, ms, amp, s_tone_buf, TONE_MAX_SAMPLES);
    if (n == 0u) return;
    /* force_mono=true, is_8bit=false: our samples are 16-bit mono. */
    (void)i2s_audio_start(s_tone_buf, (unsigned)n, true, false);
}

void hal_audio_idle(void) {
    i2s_audio_stop();
}
```

- [ ] **Step 3: Pump the I²S chain**

`i2s_audio_process()` must be called regularly or the A/B buffer chain starves and the note truncates. `hal_pump()` already runs once per main-loop iteration and is the natural home. Add to the end of `hal_pump()` in `src/hal/hal_og.c`:

```c
    i2s_audio_process();   /* feeds the A/B chain; starves and truncates without it */
```

Do **not** add a second `fwog_power_poll()` call, and do not move the existing one.

- [ ] **Step 4: Report the capability honestly**

In `hal_caps()`, change `c.audio = false; /* Plan OG-B */` to `c.audio = true;`.

- [ ] **Step 5: Build and test**

Run: `powershell -File tools/build_og.ps1`, then `powershell -File tools/test.ps1`, then `powershell -File tools/build.ps1`
Expected: all green, 12/12 host tests.

- [ ] **Step 6: Flash and verify on hardware**

Run: `powershell -File tools/flash_og.ps1`

Confirm `audio=ok` in the heartbeat on COM59.

Then, **with a human present — this step cannot be verified from a console or a camera**:
- Press grey to start a session: the start chime should sound (neon theme, `SND_START`).
- Let a session end, or press yellow (Skip) to reach a phase change: the end chime should sound.
- Judge two things and record them in Step 7: whether the chimes are **audible at a sensible level**, and whether they sound **harsh or buzzy**. At 8 kHz with a 2.5 kHz ceiling some dulling versus the FreeWili 2 is expected and acceptable; aliasing whine is not.
- If the level is wrong, adjust `TONE_PEAK_MAX` in `src/app/tone_synth.h` (its test asserts the rendered peak respects it, so the test moves with it). If the tone quality is wrong, the fix is the shared note tables in `src/app/sound.c` — but changing those affects the FreeWili 2, so treat it as its own decision rather than a tweak.

- [ ] **Step 7: Record what the chimes actually sound like**

Add a short, honest paragraph to the FreeWili OG section of `docs/hardware-notes.md`: the `TONE_PEAK_MAX` finally used, whether the chimes are comparable to the FreeWili 2 or duller, and whether any note in the shared tables had to be retuned. This is exactly the kind of claim `hardware-notes.md` exists to keep honest.

- [ ] **Step 8: Commit**

```bash
git add src/hal/hal_og.c src/target_og/main.c docs/hardware-notes.md
git commit -m "feat(og): synthesized chimes over the 8 kHz I2S path

hal_tone() renders one note into a static buffer and hands it to
i2s_audio_start(); hal_pump() feeds i2s_audio_process() so the A/B chain
does not starve mid-note. The 2.5 kHz clamp lives here rather than in
sound.c, whose note tables are shared with the FreeWili 2."
```

---

### Task 4: Tilt-to-pause via the LIS3DH

The one task that genuinely cannot be finished without a board in your hands: which axis reads "flat" is measured, not derived.

**Files:**
- Modify: `src/hal/hal_og.c`
- Modify: `src/target_og/main.c`
- Modify: `docs/hardware-notes.md`

**Interfaces:**
- Consumes: `tilt.c`'s existing gate (`src/core/tilt.c`, unchanged), which takes `ax, ay, az` in g and applies its own hysteresis.
- Produces: working `hal_imu()`; `hal_caps().imu == true`.

- [ ] **Step 1: Bring the accelerometer up**

In `src/target_og/main.c`, alongside the other peripheral init:

```c
    lis3dh_init();
    const bool imu_ok = lis3dh_configure(LIS3DH_RANGE_2G);
    if (!imu_ok) DIAG("[wilidoro] lis3dh init FAILED\n");
```

`±2 g` is the right range: tilt-to-pause cares about the 1 g gravity vector's direction, and the narrowest range gives the finest resolution for it. Extend the heartbeat to carry `imu=%s`.

- [ ] **Step 2: Implement `hal_imu()` with the axis mapping isolated**

In `src/hal/hal_og.c`, replace the IMU stub. Keep the axis mapping in one obvious place, because Step 4 will change it:

```c
/* LIS3DH at +/-2 g. The datasheet's normal-mode 10-bit sensitivity is
   4 mg/digit, and lis3dh_assemble_axis() returns the register pair as a signed
   16-bit value whose low 6 bits are not significant in that mode -- so a raw
   count is (value >> 6) * 4 mg. Expressed as g: raw / 16384.0f. */
#define LIS3DH_COUNTS_PER_G 16384.0f

/* WHICH AXIS IS "FLAT" IS BENCH-MEASURED, NOT DERIVED. The FreeWili 2's
   BMI323 sits in a different orientation, so tilt.c's convention -- board
   lying flat is the running orientation -- maps onto different axes here.
   See docs/hardware-notes.md; do not "tidy" this mapping without a board. */
bool hal_imu(float *ax, float *ay, float *az) {
    lis3dh_sample_t s;
    lis3dh_motion_t m;
    if (!lis3dh_process(LIS3DH_MOVE_THRESHOLD_DEFAULT, &s, &m)) return false;
    *ax = (float)s.x / LIS3DH_COUNTS_PER_G;
    *ay = (float)s.y / LIS3DH_COUNTS_PER_G;
    *az = (float)s.z / LIS3DH_COUNTS_PER_G;
    return true;
}
```

- [ ] **Step 3: Add a temporary axis-reporting DIAG**

So the orientation can actually be measured. In `src/target_og/main.c`'s 1 Hz heartbeat branch, temporarily add:

```c
        float ax, ay, az;
        if (hal_imu(&ax, &ay, &az)) {
            /* DIAG takes no floats -- print milli-g as integers. */
            DIAG("[wilidoro] imu mg: x=%d y=%d z=%d\n",
                 (int)(ax * 1000.0f), (int)(ay * 1000.0f), (int)(az * 1000.0f));
        }
```

Note the constraint in that comment: this tree's `DIAG()` must not be given floats or `%f`.

- [ ] **Step 4: Measure the orientation on a board**

Run: `powershell -File tools/build_og.ps1` then `powershell -File tools/flash_og.ps1`, and read COM59.

**With the board in your hands**, record the milli-g triple in each of these positions:
1. Lying **flat** on the desk, screen up — the running orientation.
2. **Tipped up** to roughly 45°, as if standing it against something.
3. Standing **vertical**, screen facing you.

The axis reading close to **+1000 mg in position 1** and falling toward 0 through positions 2 and 3 is the "flat" axis. Set the mapping in `hal_imu()` so that the value `tilt.c` sees as `az` is that axis, negating it if position 1 reads about **−1000**.

**These readings also check `LIS3DH_COUNTS_PER_G` itself.** In position 1 exactly one axis should read about ±1000 mg and the other two near 0, because the only force acting is 1 g of gravity. If instead the dominant axis reads roughly **250 mg** the constant is 4× too large; roughly **4000 mg** and it is 4× too small — either way the fix is `LIS3DH_COUNTS_PER_G`, not the axis mapping. Do not proceed to Step 5 until one axis reads about 1 g, or `tilt.c`'s thresholds will be compared against a wrongly scaled vector.

- [ ] **Step 5: Confirm tilt-to-pause end to end**

Remove the temporary `DIAG` from Step 3 — it fires once a second forever and is debug scaffolding, not a feature.

Set `c.imu = true;` in `hal_caps()`, rebuild, reflash, then:
- Start a session with the board flat. It runs.
- Tip the board up. The session **pauses**, and the timer face shows the paused state.
- Lay it flat again. It **resumes**.
- Tip it slowly through the threshold a few times: it must not chatter between running and paused at the boundary — that is what `tilt.c`'s hysteresis is for, and if it chatters the threshold or the axis mapping is wrong, not the hysteresis.

- [ ] **Step 6: Record the measured orientation**

Add to the FreeWili OG section of `docs/hardware-notes.md`: which physical axis is "flat" on this board, the milli-g triples measured in all three positions, and the sign. Anyone who touches `hal_imu()` later needs these numbers and cannot recover them without a board.

- [ ] **Step 7: Build, test, and verify the FreeWili 2**

Run: `powershell -File tools/test.ps1`, `powershell -File tools/build.ps1`, `powershell -File tools/build_og.ps1`
Expected: 12/12 host tests, both firmwares build.

- [ ] **Step 8: Commit**

```bash
git add src/hal/hal_og.c src/target_og/main.c docs/hardware-notes.md
git commit -m "feat(og): tilt-to-pause via the LIS3DH

The axis that reads flat differs from the FreeWili 2's BMI323 and is not
derivable from the datasheet -- the mapping here is bench-measured, with
the milli-g triples recorded in hardware-notes so the next person does not
need a board to understand it."
```

---

## What Plan OG-B deliberately leaves undone

- **Plan OG-C — Themes, screens and the simulator.** The arcade and flip themes at 320×240, Settings and Nearby with their Back softkeys, and the SDL simulator's OG geometry mode.
- **Plan OG-D — The beacon.** Link opcodes `0x40`/`0x41` and their host tests, the main-CPU CC1101 app, and the two-radio over-the-air self-test.

## Verification that needs a human

Listed together because three of this plan's four tasks end in a judgement no console or camera can make:

| Task | Needs | Why |
|---|---|---|
| 1 — LEDs | eyes or a camera | brightness is a bench-comfort choice |
| 3 — Audio | **ears** | level and harshness cannot be measured from a console |
| 4 — Tilt | **hands** | the board must be physically tilted, and the axis measured |

Task 2 is fully host-tested and needs neither.

## Self-review notes

Checked against `docs/superpowers/specs/2026-07-28-wilidoro-og-port-design.md`:

- Spec items covered: the 7-LED ring with software brightness; 8 kHz tone synthesis with the ≥2.5 kHz clamp *in the OG HAL, not `sound.c`*; the host tests the spec names ("the ≥2.5 kHz clamp, and that no synthesized note overruns its buffer"); tilt via the LIS3DH with the axis as a bench-measured constant; `ws2812_init()` called, which the spec's carry-forward section flags as required for the shutdown countdown; `hal_power_armed()` becoming load-bearing.
- Spec items still deferred, with a named plan: themes/screens/sim (OG-C), beacon (OG-D).
- Carry-forward item **not** addressed here: hardening `tools/flash_og.ps1` against a resetting main CPU. It is tooling rather than a peripheral, and this plan's tasks each flash a healthy board. If Task 1 or 3 hits a flash failure loop, stop and do that hardening first — the design doc records why.
