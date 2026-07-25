# Wilidoro Plan C1 — Ambient Feedback (Auto-dim + LED Patterns) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring the FreeWili's ambient hardware to life — the OPT4001 light sensor auto-dims the screen and LED brightness, and the 16 WS2812 LEDs show a per-theme session-progress pattern with a completion celebration.

**Architecture:** A new pure, host-tested `led_pattern` module renders the 16-LED array from `(theme index, timer_view, )` — decoupled from LVGL so the patterns are unit-tested. The app tick polls `hal_lux` (throttled), runs it through the already-host-tested `core/dimming` curve, and applies the result to `hal_backlight` + `hal_led_brightness`; then it renders `led_pattern` into `hal_led_*`. The device HAL gains the real OPT4001 wiring (`hal_lux`, `caps.light=true`); the sim HAL keeps a fake lux the user can vary with keys.

**Tech Stack:** C11, wilibsp (`opt4001`, `ws2812`, `bl_pwm`), `core/dimming` (done, host-tested), LVGL-free `led_pattern`, SDL sim, CTest.

## Global Constraints

- **This is Plan C1 of the Plan C hardware layer.** C1 = auto-dim + LEDs. C2 = audio, C3 = IMU gestures, C4 = CC1101 beacon — separate plans.
- **`led_pattern` is PURE and LVGL-free** (own `led_rgb_t`, no wilibsp/LVGL/hal includes) so it is host-unit-tested. It sets colors 0..255; **brightness scaling is separate** (`hal_led_brightness`, applied by the driver at show time) — `led_pattern` never pre-scales.
- **Reuse, don't reinvent:** the lux→backlight% curve is `core/dimming` (`dim_init`, `dim_apply`, `dim_led_brightness`), already host-tested in Plan A. C1 wires it; it does not re-implement the curve.
- **HAL is the only hardware seam.** `led_pattern`, `app`, `core` never call wilibsp directly. `hal_lux`/`hal_backlight`/`hal_led_*` are the seam.
- **Theme index is the shared key** between the LVGL theme (`app_settings_t.theme`, 0=neon/1=arcade/2=flip) and `led_pattern`. `led_pattern` does NOT include `theme.h`.
- **LED refresh quirk:** device must `ws2812_show()` periodically (the app tick already calls `hal_led_show()` every tick — keep that).
- **Auto-dim throttle:** poll lux at ~2 Hz (every 500 ms), not every tick (OPT4001 read is blocking I2C). Backlight/LED brightness update at that rate; the smoothing in `dim_apply` handles gradual change.
- **Hardware access rule:** builds + host tests + sim run freely. Flashing/RTT/camera require asking the user first — on-device visual acceptance is user-gated.
- **Commit trailer:** `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

## Verification model

- `led_pattern` → host CTest (autonomous).
- Auto-dim + LED wiring → build gates (both targets link, no overflow/warnings) + host suite + sim launch smoke. The actual dimming and LED lighting are **user-gated** (sim shows LEDs? no — sim has no LED render in C1; so LED behavior is verified by the host tests + on-device when the user approves a flash). State this honestly.

---

## File Structure

```
src/app/
  led_pattern.h/.c   NEW   PURE host-tested: render 16 LEDs from (theme_idx, timer_view_t)
src/hal/
  hal_target.c       MOD   opt4001_init in hal_init; hal_lux via opt4001_read; caps.light=true
  hal_sim.c          MOD   fake lux is a variable; '[' / ']' keys lower/raise it (demo)
src/app/
  app.c              MOD   tick: throttled lux->dim_apply->backlight+led_brightness; led_pattern->hal_led_*
tests/
  test_led_pattern.c NEW   idle off, focus fills proportional, break color, alarm celebration, paused dim
  CMakeLists.txt     MOD   add test_led_pattern (compiles led_pattern.c + timer_view.c + pomodoro.c)
CMakeLists.txt / src/sim/CMakeLists.txt  MOD   add src/app/led_pattern.c to both targets
```

---

### Task 1: `led_pattern` pure module (TDD)

**Files:**
- Create: `src/app/led_pattern.h`, `src/app/led_pattern.c`, `tests/test_led_pattern.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `timer_view.h` (`timer_view_t`).
- Produces (verbatim header):
```c
// src/app/led_pattern.h
#ifndef WILIDORO_LED_PATTERN_H
#define WILIDORO_LED_PATTERN_H
#include <stdint.h>
#include "timer_view.h"

#define LED_COUNT 16
typedef struct { uint8_t r, g, b; } led_rgb_t;

/* Render the 16-LED array for the given theme (0=neon,1=arcade,2=flip) and timer state.
   Sets full 0..255 colors; brightness is applied separately by the driver. */
void led_pattern_render(uint8_t theme_idx, const timer_view_t *v, led_rgb_t out[LED_COUNT]);
#endif
```
Semantics (tests encode): let `elapsed_permille = total_ms ? (total_ms - rem_ms)*1000/total_ms : 0`; `lit = elapsed_permille * LED_COUNT / 1000` (0..16). Per-theme colors from a table: focus, break, celebrate. If `v->idle`: all LEDs off (0,0,0). Else if `v->alarm`: all LEDs = the theme's celebrate color. Else: LEDs `[0, lit)` = phase color (`break_phase?break:focus`), halved (>>1 each channel) when `v->paused`; LEDs `[lit, 16)` off.

- [ ] **Step 1: Write the failing tests**

`tests/test_led_pattern.c`:
```c
#include "greatest.h"
#include "led_pattern.h"
#include <string.h>

static timer_view_t V(bool idle, bool paused, bool alarm, bool brk, uint32_t total, uint32_t rem) {
    timer_view_t v = {0};
    v.idle=idle; v.paused=paused; v.alarm=alarm; v.break_phase=brk;
    v.total_ms=total; v.rem_ms=rem;
    return v;
}
static int lit_count(const led_rgb_t *o) {
    int n=0; for (int i=0;i<LED_COUNT;i++) if (o[i].r||o[i].g||o[i].b) n++; return n;
}

TEST idle_all_off(void) {
    led_rgb_t o[LED_COUNT];
    timer_view_t v = V(true,false,false,false, 1500000, 1500000);
    led_pattern_render(0, &v, o);
    ASSERT_EQ(0, lit_count(o));
    PASS();
}

TEST focus_fills_proportional(void) {
    led_rgb_t o[LED_COUNT];
    /* 50% elapsed: total=1000ms, rem=500ms -> 8 LEDs lit */
    timer_view_t v = V(false,false,false,false, 1000, 500);
    led_pattern_render(0, &v, o);
    ASSERT_EQ(8, lit_count(o));
    /* neon focus color is warm (r dominant) */
    ASSERT(o[0].r > o[0].b);
    PASS();
}

TEST break_uses_cool_color(void) {
    led_rgb_t o[LED_COUNT];
    timer_view_t v = V(false,false,false,true, 1000, 750);  /* 25% -> 4 lit */
    led_pattern_render(0, &v, o);
    ASSERT_EQ(4, lit_count(o));
    ASSERT(o[0].b > o[0].r);   /* neon break color is cool (blue/teal dominant) */
    PASS();
}

TEST alarm_lights_all(void) {
    led_rgb_t o[LED_COUNT];
    timer_view_t v = V(false,false,true,false, 1000, 0);
    led_pattern_render(1, &v, o);   /* arcade celebrate */
    ASSERT_EQ(16, lit_count(o));
    PASS();
}

TEST paused_is_dimmer_than_running(void) {
    led_rgb_t run[LED_COUNT], pau[LED_COUNT];
    timer_view_t vr = V(false,false,false,false, 1000, 500);
    timer_view_t vp = V(false,true, false,false, 1000, 500);
    led_pattern_render(0, &vr, run);
    led_pattern_render(0, &vp, pau);
    ASSERT_EQ(lit_count(run), lit_count(pau));   /* same count */
    ASSERT(pau[0].r < run[0].r);                 /* but dimmer */
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(idle_all_off);
    RUN_TEST(focus_fills_proportional);
    RUN_TEST(break_uses_cool_color);
    RUN_TEST(alarm_lights_all);
    RUN_TEST(paused_is_dimmer_than_running);
    GREATEST_MAIN_END();
}
```
Add to `tests/CMakeLists.txt`:
```cmake
add_executable(test_led_pattern test_led_pattern.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/led_pattern.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/timer_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/pomodoro.c)
target_include_directories(test_led_pattern PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CORE} ${CMAKE_CURRENT_SOURCE_DIR}/../src/app)
target_compile_options(test_led_pattern PRIVATE -Wall -Wextra)
target_link_libraries(test_led_pattern m)
add_test(NAME test_led_pattern COMMAND test_led_pattern)
```

- [ ] **Step 2: Run — verify build failure**

Run: `powershell -File tools/test.ps1`
Expected: FAILS (`led_pattern.h` not found).

- [ ] **Step 3: Write `led_pattern.h` (verbatim above) and `led_pattern.c`**

`src/app/led_pattern.c`:
```c
#include "led_pattern.h"
#include <string.h>

typedef struct { led_rgb_t focus, brk, celebrate; } palette_t;
static const palette_t PALETTES[3] = {
    /* neon   */ { {255,91,69},  {45,212,191}, {255,150,80} },
    /* arcade */ { {255,71,87},  {46,213,115}, {255,224,102} },
    /* flip   */ { {200,120,60}, {138,125,107},{240,200,120} },
};

void led_pattern_render(uint8_t theme_idx, const timer_view_t *v, led_rgb_t out[LED_COUNT]) {
    const palette_t *p = &PALETTES[theme_idx < 3 ? theme_idx : 0];
    memset(out, 0, sizeof(led_rgb_t) * LED_COUNT);
    if (v->idle) return;
    if (v->alarm) {
        for (int i = 0; i < LED_COUNT; i++) out[i] = p->celebrate;
        return;
    }
    uint32_t elapsed = v->total_ms ? (uint32_t)((uint64_t)(v->total_ms - v->rem_ms) * 1000 / v->total_ms) : 0;
    int lit = (int)(elapsed * LED_COUNT / 1000);
    if (lit > LED_COUNT) lit = LED_COUNT;
    led_rgb_t c = v->break_phase ? p->brk : p->focus;
    if (v->paused) { c.r >>= 1; c.g >>= 1; c.b >>= 1; }
    for (int i = 0; i < lit; i++) out[i] = c;
}
```

- [ ] **Step 4: Run — verify green (5 cases, suite now 8 binaries)**

Run: `powershell -File tools/test.ps1`
Expected: `test_led_pattern` passes all 5; ctest 100%.

- [ ] **Step 5: Commit**

```bash
git add src/app/led_pattern.h src/app/led_pattern.c tests/test_led_pattern.c tests/CMakeLists.txt
git commit -m "feat(app): led_pattern (per-theme LED render) with tests"
```

---

### Task 2: Wire auto-dim + LED patterns into the app + HALs

**Files:**
- Modify: `src/hal/hal_target.c`, `src/hal/hal_sim.c`, `src/app/app.c`, `CMakeLists.txt`, `src/sim/CMakeLists.txt`

**Interfaces:**
- Consumes: `core/dimming.h` (`dim_init`, `dim_apply`, `dim_led_brightness`), `led_pattern.h` (Task 1), `timer_view.h`, `hal.h`.

- [ ] **Step 1: Device HAL — real OPT4001 lux + light cap**

In `src/hal/hal_target.c`: add `#include "sensors/opt4001.h"`. In `hal_init`, after the existing setup, add `bool light_ok = opt4001_init();` and store it in a file-static `static bool s_light;` (`s_light = opt4001_init();`). Change `hal_lux` to:
```c
bool hal_lux(float *lux) { return s_light && opt4001_read(lux); }
```
Change `hal_caps` to report `.light = s_light` instead of `false`. (Leave imu/audio/radio false — those are C2/C3/C4.)

- [ ] **Step 2: Sim HAL — variable fake lux with keys**

In `src/hal/hal_sim.c`: add a file-static `static float s_lux = 300.0f;`. In `map_key`, add nothing (keys handled in the watch). In `key_watch`, before/after the button mapping, handle two demo keys (these are NOT buttons, so don't push to the queue):
```c
        if (e->key.keysym.scancode == SDL_SCANCODE_LEFTBRACKET)  s_lux = (s_lux > 20.f) ? s_lux - 40.f : 0.f;
        if (e->key.keysym.scancode == SDL_SCANCODE_RIGHTBRACKET) s_lux = (s_lux < 960.f) ? s_lux + 40.f : 1000.f;
```
(Place these inside the `if (e->type == SDL_KEYDOWN && e->key.repeat == 0)` block, alongside the existing `map_key` call.) Change `hal_lux` to `{ *lux = s_lux; return true; }`.

- [ ] **Step 3: App — throttled auto-dim + LED render each tick**

In `src/app/app.c`: add includes `#include "dimming.h"` and `#include "led_pattern.h"` and `#include "timer_view.h"`. Add file-statics near `s_app`:
```c
static dim_state_t s_dim;
static uint32_t s_next_lux;
```
In `app_init`, after `neighbor_table_init`, add:
```c
    dim_init(&s_dim, 100.0f);
    s_next_lux = 0;
```
In `tick_cb`, REPLACE the existing hardcoded phase-LED block (the `uint8_t r=0,g=0,bl=0; ... for(i)hal_led_set(...); hal_led_show();` section) with:
```c
    /* auto-dim: poll lux at ~2 Hz through the core dimming curve */
    if (now >= s_next_lux) {
        float lux;
        if (hal_lux(&lux)) {
            uint8_t pct = dim_apply(&s_dim, lux);
            hal_backlight(pct);
            hal_led_brightness(dim_led_brightness(pct));
        }
        s_next_lux = now + 500;
    }
    /* per-theme LED pattern */
    timer_view_t lv = timer_view_make(&s_app.pomo, s_app.alarm_active, now);
    led_rgb_t leds[LED_COUNT];
    led_pattern_render(s_app.settings.theme, &lv, leds);
    for (int i = 0; i < LED_COUNT; i++) hal_led_set(i, leds[i].r, leds[i].g, leds[i].b);
    hal_led_show();
```
(Keep the rest of `tick_cb` — pomodoro_tick, button routing, beacon rx, screen refresh — unchanged.)

- [ ] **Step 4: CMake — add `led_pattern.c` to both targets**

Root `CMakeLists.txt`: add `src/app/led_pattern.c` to `add_executable(wilidoro ...)`.
Sim `src/sim/CMakeLists.txt`: add `${CMAKE_CURRENT_SOURCE_DIR}/../app/led_pattern.c` to `add_executable(wilidoro_sim ...)`.

- [ ] **Step 5: Build both + host tests**

Run `powershell -File tools/test.ps1` (8/8). Device build (`tools/build.ps1 -Clean`) links, no overflow, no warnings. Sim build links, no warnings. Paste all + artifacts.

- [ ] **Step 6: Commit**

```bash
git add src/hal/hal_target.c src/hal/hal_sim.c src/app/app.c CMakeLists.txt src/sim/CMakeLists.txt
git commit -m "feat(app): auto-dim (OPT4001->dimming->backlight) + per-theme LED patterns"
```

---

### Task 3: Integration pass + sim smoke + checkpoint

**Files:** none expected (small fixes only if the build surfaced something).

- [ ] **Step 1: Full host suite + both clean builds**

Run `powershell -File tools/test.ps1` (8/8). Device + sim builds link clean, no warnings, no SRAM overflow. Paste all + artifacts.

- [ ] **Step 2: Sim launch smoke**

Launch `build-sim/wilidoro_sim.exe` in the background ~4s, confirm alive (no crash), terminate. Report. (No hardware.)

- [ ] **Step 3: User-checkpoint note**

In the report, write how to verify: host tests cover the LED pattern logic and dim curve; on-device (user-gated flash) the screen should auto-dim as room light changes and the 16 LEDs fill with the theme color as a focus/break session progresses, going full-strip on completion. In the sim, `[` / `]` vary the fake lux (backlight has no visible effect in the sim, but the app runs the path). Note the sim has no on-screen LED strip in C1 (a Plan-polish nicety), so LED visual confirmation is on-device only.

- [ ] **Step 4: Commit (if any fixes) / else note clean**

```bash
git add -A
git commit -m "chore(app): Plan C1 integration pass — ambient feedback wired, both build" --allow-empty
```

---

## Self-Review

**Spec coverage (C1 scope):** auto-dim (OPT4001 → `core/dimming` → backlight + LED brightness, Task 2) and the 16-LED session-progress pattern + completion celebration per theme (Tasks 1–2). The spec's "LED brightness scales with the dim curve" and "each theme fires its own short LED celebration" are met (celebration = full-strip theme color on alarm; per-theme palettes). Deferred within Plan C: audio (C2), gestures (C3), radio (C4); and the richer per-theme LED *animations* (breathing/chase) beyond the progress fill are a later nicety — stated so they aren't mistaken for gaps.

**Placeholder scan:** none. Task 1 carries full test + impl. Task 2's edits are concrete diffs against named existing code (the hardcoded phase-LED block being replaced).

**Type consistency:** `led_rgb_t`/`LED_COUNT`/`led_pattern_render` match across led_pattern.h, the test, and app.c. `dim_state_t`/`dim_init`/`dim_apply`/`dim_led_brightness` match `core/dimming.h` (Plan A). `hal_lux`/`hal_backlight`/`hal_led_brightness`/`hal_led_set`/`hal_led_show` match `hal.h`. Theme index passed to `led_pattern_render` is `app_settings_t.theme` (uint8_t), consistent with the theme registry.

**Risk called out:** the device OPT4001 read is blocking I2C — the 500 ms throttle keeps it off the per-tick path. `opt4001_init()` may fail (sensor absent) → `hal_lux` returns false → auto-dim simply doesn't run (backlight stays at the `bl_pwm_init` default 100%), which is a safe degradation; `caps.light` reflects it.

## Roadmap after C1

- **Plan C2 — Audio:** NAU88C10/I2S synthesized tones — per-theme start chimes, end alarms, optional focus tick, touch blips — wired to `hal_tone`/`hal_audio_idle`; the theme's sound set keyed by theme index.
- **Plan C3 — IMU gestures:** BMI323 → `hal_imu` → `core/gestures` (done, host-tested) → app actions (flip-to-start, flip-face-down deep focus, shake-to-dismiss-alarm, pick-up-to-wake).
- **Plan C4 — CC1101 beacon:** real TX (`beacon_ook_encode` → `ook_tx_send`) every ~10 s gated by `beacon_on`, and RX (`gdo_capture` → `beacon_ook_decode`) populating the neighbor table; includes migrating the LVGL flush to `st7796_flush_async` + SPI1 bus arbitration (the seam noted since Plan A) so the display and radio can share SPI1.
