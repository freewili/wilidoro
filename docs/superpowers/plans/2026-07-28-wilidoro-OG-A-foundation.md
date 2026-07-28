# Wilidoro OG Plan A — Foundation & First Face — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the wilidoro repo build for a second board, and get a running pomodoro timer face — one theme, working buttons, correct power policy — on the FreeWili OG's display CPU.

**Architecture:** One repo, two board targets selected by `-DWILIDORO_BOARD=fw2|og` at configure time (`PICO_BOARD` is a single global cache value and the boards are different silicon, so one configure cannot build both). `src/core/` and `src/app/` are shared verbatim; the OG adds a HAL backend (`hal_og.c`), an LVGL port against ST7789, and a main-CPU app that exists in this plan only as the vehicle that carries the display image onto the board.

**Tech Stack:** C11, Pico SDK 2.3.0 (RP2040), LVGL 9.2.2, CMake + Ninja, `wiliOGbsp` BSP, CTest + greatest.h.

**Spec:** `docs/superpowers/specs/2026-07-28-wilidoro-og-port-design.md`

## Global Constraints

Every task's requirements implicitly include this section. Values copied verbatim from `wiliOGbsp/AGENTS.md` and the spec.

- **Target MCU:** RP2040 ×2 — a *display* CPU and a *main* CPU. `PICO_BOARD` is set to `freewili_og` in CMake ONLY. **Never pass `-DPICO_BOARD` on a command line.**
- **Clock:** the BSP runs at 200 MHz (vreg 1.15 V, flash `CLKDIV` 4). Never hardcode a PIO divider or a baud rate — derive from `clock_get_hz()`.
- **UART0 on both CPUs is the inter-CPU link and must never carry stdio.** Every app calls `fwog_configure_stdio(<target>)` (done for you by `fwog_display_app()` / `fwog_main_app()`). **Never `printf`** — use `DIAG()`.
- **Never add a watchdog to the display CPU.**
- **The main CPU's watchdog is armed by `board_init()` and every main-CPU app
  MUST call `board_watchdog_kick()` every loop iteration** (2 s window,
  `FWOG_WATCHDOG_MS`). It is the only way to recover a hung main CPU on this
  board, so the BSP makes kicking it the app's job. Omitting it neither fails
  to build nor fails to run — main simply resets every 2 s forever, taking the
  display with it via `board_init()`'s `GUI_NRESET`. This was measured on the
  first hardware bring-up, not theorised. `apps/template_main/main.c` is the
  reference; `template_display` is **not**, and copying it is how this was
  missed.
- **Every display app must declare a power policy or it does not link.** We use `FWOG_POWER_DEFAULT()`, then call `fwog_power_poll(now_ms)` exactly once per main-loop iteration and take buttons from its **return value** — a second `fwog_buttons_poll()` in the same iteration consumes edges out from under the ship-mode machine.
- **`fwog_power_poll()` renders the shutdown countdown on the WS2812 bar itself** when a hold is armed, restoring previous colours if aborted. App LED writes must not fight it — see Task 3.
- **`PIN_LCD_DC` is 12 and `PIN_LCD_CS` is 13**, despite the legacy names suggesting otherwise.
- **Never flash the display app by UF2:**
  ```
  fw flash wilidoro_main       # correct, always
  fw flash wilidoro_display    # BRICKS THE DISPLAY CPU
  ```
  A display UF2 links at `0x10021000` and a raw copy does not write the metadata sector at `0x10020000`, so `app_valid` fails, the app never runs, and the display CPU — the one CPU with no BOOTSEL button — stops enumerating. Task 2 puts a guard in tooling.
- **App declaration:** `fwog_display_app(target VERSION 001 DESCRIPTION "...")`. `VERSION` is exactly three digits; `DESCRIPTION` is required and has no default. Missing or malformed is a configure error.
- **`src/core/` purity:** files under `src/core/` include only the C standard library and their own headers. Time is always a `uint32_t now_ms` parameter; `core/` never reads a clock.
- **FW2 must not regress.** Every task ends with the FW2 build and the full host test suite still green.
- **Commit discipline:** every task ends on a green build/test and a commit. Trailer:
  `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`

---

## Integration constraint discovered during planning

`wiliOGbsp/bsp/CMakeLists.txt` invokes its Python helpers as
`${CMAKE_SOURCE_DIR}/tools/<name>.py` — `gen_uf2_info.py`, `check_uf2_info.py`
and `genimage.py`. `CMAKE_SOURCE_DIR` is the **top-level** source dir, which in
this repo is `wilidoro/`, not `wilidoro/wiliOGbsp/`. Consumed as a submodule
the BSP therefore looks for those three scripts in *our* `tools/` and fails the
build if they are absent.

Task 2 resolves this with three ~6-line shims in `tools/` that delegate to the
submodule's copies via `runpy`. No logic is duplicated and the BSP is not
patched, so submodule updates stay clean.

---

## File Structure

```
wilidoro/
  wiliOGbsp/                       submodule -> github.com/freewili/wiliOGbsp   (NEW)
  cmake/
    board_fw2.cmake                FW2 targets, moved out of the root file      (NEW)
    board_og.cmake                 OG targets: display app + main app           (NEW)
  src/
    app/
      led_pattern.h  led_pattern.c count is now a parameter                     (MODIFIED)
    hal/
      hal.h                        + hal_led_count()                            (MODIFIED)
      hal_target.c                 + hal_led_count() -> 16                      (MODIFIED)
      hal_sim.c                    + hal_led_count() -> 16                      (MODIFIED)
      hal_og.c                     OG display-CPU HAL backend                   (NEW)
    target_og/
      main.c                       OG display-CPU entry                         (NEW)
      lvgl_port_og.c  .h           LVGL <-> ST7789 + 5 buttons                  (NEW)
    main_og/
      main.c                       OG main-CPU app (display image carrier)      (NEW)
  tests/
    test_led_pattern.c             parameterised over count                     (MODIFIED)
  tools/
    build_og.ps1                   configure + build the OG pair                (NEW)
    flash_og.ps1                   flash wilidoro_main, with the display guard  (NEW)
    gen_uf2_info.py                shim -> wiliOGbsp/tools/                     (NEW)
    check_uf2_info.py              shim -> wiliOGbsp/tools/                     (NEW)
    genimage.py                    shim -> wiliOGbsp/tools/                     (NEW)
  CMakeLists.txt                   board selection + shared LVGL setup          (MODIFIED)
  CMakePresets.json                + og preset                                  (MODIFIED)
```

---

### Task 1: `led_pattern_render` takes an explicit LED count

Pure logic, host-tested, no board and no build-system risk. Doing it first
means the shared code is already count-agnostic before the OG build exists.
The OG has 7 LEDs; FW2 has 16.

**Files:**
- Modify: `src/app/led_pattern.h`, `src/app/led_pattern.c`
- Modify: `src/hal/hal.h`, `src/hal/hal_target.c`, `src/hal/hal_sim.c`
- Modify: `src/app/app.c:219-221`
- Test: `tests/test_led_pattern.c`

**Interfaces:**
- Produces: `#define LED_COUNT_MAX 16`;
  `void led_pattern_render(uint8_t theme_idx, const timer_view_t *v, led_rgb_t *out, int count);`
  and `int hal_led_count(void);` — Task 3 implements the latter for the OG,
  returning 7.

- [ ] **Step 1: Write the failing tests**

Replace the whole of `tests/test_led_pattern.c` with the version below. It
parameterises `lit_count` over a count, keeps every existing assertion at
count 16, and adds two OG-shaped cases at count 7.

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
static int lit_count(const led_rgb_t *o, int n) {
    int c=0; for (int i=0;i<n;i++) if (o[i].r||o[i].g||o[i].b) c++; return c;
}

TEST idle_all_off(void) {
    led_rgb_t o[LED_COUNT_MAX];
    timer_view_t v = V(true,false,false,false, 1500000, 1500000);
    led_pattern_render(0, &v, o, 16);
    ASSERT_EQ(0, lit_count(o, 16));
    PASS();
}

TEST focus_fills_proportional(void) {
    led_rgb_t o[LED_COUNT_MAX];
    /* 50% elapsed: total=1000ms, rem=500ms -> 8 of 16 lit */
    timer_view_t v = V(false,false,false,false, 1000, 500);
    led_pattern_render(0, &v, o, 16);
    ASSERT_EQ(8, lit_count(o, 16));
    ASSERT(o[0].r > o[0].b);   /* neon focus is warm */
    PASS();
}

TEST break_uses_cool_color(void) {
    led_rgb_t o[LED_COUNT_MAX];
    timer_view_t v = V(false,false,false,true, 1000, 750);  /* 25% -> 4 of 16 */
    led_pattern_render(0, &v, o, 16);
    ASSERT_EQ(4, lit_count(o, 16));
    ASSERT(o[0].b > o[0].r);   /* neon break is cool */
    PASS();
}

TEST alarm_lights_all(void) {
    led_rgb_t o[LED_COUNT_MAX];
    timer_view_t v = V(false,false,true,false, 1000, 0);
    led_pattern_render(1, &v, o, 16);   /* arcade celebrate */
    ASSERT_EQ(16, lit_count(o, 16));
    PASS();
}

TEST paused_is_dimmer_than_running(void) {
    led_rgb_t run[LED_COUNT_MAX], pau[LED_COUNT_MAX];
    timer_view_t vr = V(false,false,false,false, 1000, 500);
    timer_view_t vp = V(false,true, false,false, 1000, 500);
    led_pattern_render(0, &vr, run, 16);
    led_pattern_render(0, &vp, pau, 16);
    ASSERT_EQ(lit_count(run,16), lit_count(pau,16));
    ASSERT(pau[0].r < run[0].r);
    PASS();
}

/* ---- OG geometry: 7 LEDs ---- */

TEST og_seven_fills_proportional(void) {
    led_rgb_t o[LED_COUNT_MAX];
    /* 4/7 elapsed -> 4 lit. 571/1000 * 7 = 3.99 -> use an exact quarter instead:
       25% of 7 = 1.75 -> 1 lit; 50% -> 3 lit (3.5 truncates). */
    timer_view_t v = V(false,false,false,false, 1000, 500);
    led_pattern_render(0, &v, o, 7);
    ASSERT_EQ(3, lit_count(o, 7));
    PASS();
}

TEST og_seven_alarm_lights_all_seven(void) {
    led_rgb_t o[LED_COUNT_MAX];
    timer_view_t v = V(false,false,true,false, 1000, 0);
    led_pattern_render(1, &v, o, 7);
    ASSERT_EQ(7, lit_count(o, 7));
    PASS();
}

TEST og_seven_does_not_touch_slack(void) {
    led_rgb_t o[LED_COUNT_MAX];
    /* Sentinel the tail: rendering at count 7 must leave indices 7..15 alone. */
    for (int i = 0; i < LED_COUNT_MAX; i++) { o[i].r = 0xAB; o[i].g = 0xCD; o[i].b = 0xEF; }
    timer_view_t v = V(false,false,true,false, 1000, 0);   /* alarm: writes every led */
    led_pattern_render(1, &v, o, 7);
    for (int i = 7; i < LED_COUNT_MAX; i++) {
        ASSERT_EQ(0xAB, o[i].r); ASSERT_EQ(0xCD, o[i].g); ASSERT_EQ(0xEF, o[i].b);
    }
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
    RUN_TEST(og_seven_fills_proportional);
    RUN_TEST(og_seven_alarm_lights_all_seven);
    RUN_TEST(og_seven_does_not_touch_slack);
    GREATEST_MAIN_END();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `powershell -File tools/test.ps1`
Expected: FAIL — compile error, `LED_COUNT_MAX` undeclared and
`led_pattern_render` takes 3 arguments, not 4.

- [ ] **Step 3: Change the header**

`src/app/led_pattern.h` — replace `#define LED_COUNT 16` and the prototype:

```c
// src/app/led_pattern.h
#ifndef WILIDORO_LED_PATTERN_H
#define WILIDORO_LED_PATTERN_H
#include <stdint.h>
#include "timer_view.h"

/* Largest ring any supported board has: FreeWili 2 has 16, the OG has 7.
   Callers size stack arrays with this and pass the live count explicitly, so
   the renderer is board-agnostic and the host tests can cover both. */
#define LED_COUNT_MAX 16
typedef struct { uint8_t r, g, b; } led_rgb_t;

/* Render `count` LEDs for the given theme (0=neon,1=arcade,2=flip) and timer
   state. Writes exactly out[0..count-1] and never touches out[count..].
   Sets full 0..255 colors; brightness is applied separately by the driver. */
void led_pattern_render(uint8_t theme_idx, const timer_view_t *v, led_rgb_t *out, int count);
#endif
```

- [ ] **Step 4: Change the implementation**

`src/app/led_pattern.c` — replace the function body. The palette table above
it is unchanged.

```c
void led_pattern_render(uint8_t theme_idx, const timer_view_t *v, led_rgb_t *out, int count) {
    if (count <= 0) return;
    if (count > LED_COUNT_MAX) count = LED_COUNT_MAX;
    const palette_t *p = &PALETTES[theme_idx < 3 ? theme_idx : 0];
    memset(out, 0, sizeof(led_rgb_t) * (size_t)count);
    if (v->idle) return;
    if (v->alarm) {
        for (int i = 0; i < count; i++) out[i] = p->celebrate;
        return;
    }
    uint32_t elapsed = v->total_ms ? (uint32_t)((uint64_t)(v->total_ms - v->rem_ms) * 1000 / v->total_ms) : 0;
    int lit = (int)(elapsed * (uint32_t)count / 1000);
    if (lit > count) lit = count;
    led_rgb_t c = v->break_phase ? p->brk : p->focus;
    if (v->paused) { c.r >>= 1; c.g >>= 1; c.b >>= 1; }
    for (int i = 0; i < lit; i++) out[i] = c;
}
```

- [ ] **Step 5: Add `hal_led_count()` to the HAL seam**

In `src/hal/hal.h`, in the LED block, change the comment and add the accessor:

```c
/* LEDs. Count is board-dependent: 16 on FreeWili 2, 7 on the OG. */
int  hal_led_count(void);
void hal_led_set(int i, uint8_t r, uint8_t g, uint8_t b);
void hal_led_brightness(uint8_t level);   /* 0..255 */
void hal_led_show(void);
```

In `src/hal/hal_target.c` and `src/hal/hal_sim.c`, add to each:

```c
int hal_led_count(void) { return 16; }
```

- [ ] **Step 6: Update the caller**

`src/app/app.c` — the block at lines 219-221 becomes:

```c
    led_rgb_t leds[LED_COUNT_MAX];
    const int nled = hal_led_count();
    led_pattern_render(s_app.settings.theme, &lv, leds, nled);
    for (int i = 0; i < nled; i++) hal_led_set(i, leds[i].r, leds[i].g, leds[i].b);
    hal_led_show();
```

Check the declaration of `leds` earlier in that function — if it is declared at
the top of the function as `led_rgb_t leds[LED_COUNT];`, change it to
`LED_COUNT_MAX` there instead of redeclaring it here.

- [ ] **Step 7: Run the tests to verify they pass**

Run: `powershell -File tools/test.ps1`
Expected: PASS — all 11 test binaries green, `test_led_pattern` now running 8
tests where it ran 5.

- [ ] **Step 8: Verify the FW2 firmware still builds**

Run: `powershell -File tools/build.ps1`
Expected: `build/wilidoro.uf2` produced, no warnings about `led_pattern_render`.

- [ ] **Step 9: Commit**

```bash
git add src/app/led_pattern.h src/app/led_pattern.c src/hal/hal.h \
        src/hal/hal_target.c src/hal/hal_sim.c src/app/app.c tests/test_led_pattern.c
git commit -m "refactor: make led_pattern_render count-agnostic

The OG has 7 LEDs where the FreeWili 2 has 16. Pass the count explicitly
and expose it through the HAL rather than baking 16 into a macro, so the
renderer is shared and the host tests cover both geometries."
```

---

### Task 2: Two board targets, and an OG pair that flashes

Ends with a display app that boots and prints a heartbeat over `DIAG()`, put
onto the board the only way a display app may be put there — carried by a main
app.

**Files:**
- Create: `wiliOGbsp/` (submodule), `cmake/board_fw2.cmake`, `cmake/board_og.cmake`
- Create: `tools/gen_uf2_info.py`, `tools/check_uf2_info.py`, `tools/genimage.py`
- Create: `tools/build_og.ps1`, `tools/flash_og.ps1`
- Create: `src/target_og/main.c`, `src/main_og/main.c`
- Modify: `CMakeLists.txt`, `CMakePresets.json`, `.gitmodules`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: CMake targets `wilidoro_display` and `wilidoro_main`; the configure
  option `WILIDORO_BOARD` (`fw2` default, `og`); `tools/build_og.ps1` and
  `tools/flash_og.ps1`. Task 3 and Task 4 add source files to the
  `wilidoro_display` target defined in `cmake/board_og.cmake`.

- [ ] **Step 1: Add the submodule**

```bash
git submodule add https://github.com/freewili/wiliOGbsp.git wiliOGbsp
git submodule update --init --recursive wiliOGbsp
```

Verify `wiliOGbsp/bsp/CMakeLists.txt` and `wiliOGbsp/tools/gen_uf2_info.py` exist.

- [ ] **Step 2: Add the three Python shims**

The BSP resolves these against `CMAKE_SOURCE_DIR`, which is this repo. Create
all three; they differ only in the script name.

`tools/gen_uf2_info.py`:

```python
#!/usr/bin/env python3
"""Shim. wiliOGbsp/bsp/CMakeLists.txt invokes ${CMAKE_SOURCE_DIR}/tools/<name>.py,
and CMAKE_SOURCE_DIR is this repo rather than the submodule. Delegate to the
submodule's copy so nothing is duplicated and submodule bumps stay clean."""
import pathlib, runpy, sys

_target = pathlib.Path(__file__).resolve().parent.parent / "wiliOGbsp" / "tools" / "gen_uf2_info.py"
sys.argv[0] = str(_target)
runpy.run_path(str(_target), run_name="__main__")
```

`tools/check_uf2_info.py` and `tools/genimage.py`: identical, with
`gen_uf2_info.py` replaced by `check_uf2_info.py` and `genimage.py` respectively.

- [ ] **Step 3: Split the existing FW2 build out of the root CMakeLists**

Create `cmake/board_fw2.cmake` containing everything from the current
`CMakeLists.txt` starting at the `# wilibsp BSP static library` comment through
the final `pico_add_extra_outputs(wilidoro)` — moved verbatim, no edits.

- [ ] **Step 4: Rewrite the root CMakeLists to select a board**

Replace `CMakeLists.txt` entirely:

```cmake
cmake_minimum_required(VERSION 3.20)

# Which board this configure builds. PICO_BOARD is a single global cache value
# and the two boards are different silicon, so one configure cannot build both.
set(WILIDORO_BOARD "fw2" CACHE STRING "Target board: fw2 (FreeWili 2) or og (FreeWili OG)")
set_property(CACHE WILIDORO_BOARD PROPERTY STRINGS fw2 og)

if(WILIDORO_BOARD STREQUAL "og")
    # Never pass -DPICO_BOARD on a command line; it silently reverts the config.
    set(PICO_BOARD freewili_og CACHE STRING "Board type" FORCE)
    list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/wiliOGbsp/bsp/boards")
    # UART0 is the inter-CPU link on BOTH CPUs and must never carry stdio.
    set(PICO_STDIO_UART 0 CACHE BOOL "stdio over UART (forced off: UART0 is the link)" FORCE)
elseif(WILIDORO_BOARD STREQUAL "fw2")
    set(PICO_BOARD freewili2 CACHE STRING "Board type")
    list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/boards")
    list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/wilibsp/bsp/boards")
else()
    message(FATAL_ERROR "WILIDORO_BOARD must be 'fw2' or 'og', got '${WILIDORO_BOARD}'")
endif()

include(pico_sdk_import.cmake)
project(wilidoro C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

# LVGL — shared by both boards; lv_conf.h branches on board where it must.
set(LV_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/config/lv_conf.h CACHE STRING "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/lvgl)

if(WILIDORO_BOARD STREQUAL "og")
    include(cmake/board_og.cmake)
else()
    include(cmake/board_fw2.cmake)
endif()
```

- [ ] **Step 5: Write the OG build**

Create `cmake/board_og.cmake`. The git-describe blocks are required: the BSP's
`fwog_add_uf2_info()` and `fwog_embed_display_image()` read
`FWOG_GIT_DESCRIBE` and `FWOG_GIT_COMMIT_TS` from the enclosing scope.

```cmake
# FreeWili OG: two RP2040s, so two executables. The display app is declared
# FIRST -- fwog_embed_display_image() resolves FWOG_DISPLAY_FIRMWARE to a
# target at configure time, so a display app declared after the main app
# cannot be selected as the payload.

# genimage.py runs at build time to embed the display firmware.
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Read by the BSP's fwog_add_uf2_info() / fwog_embed_display_image().
execute_process(
    COMMAND git describe --always --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    OUTPUT_VARIABLE FWOG_GIT_DESCRIBE OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0 OR FWOG_GIT_DESCRIBE STREQUAL "")
    set(FWOG_GIT_DESCRIBE "dev")
endif()
execute_process(
    COMMAND git log -1 --format=%ct
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    OUTPUT_VARIABLE FWOG_GIT_COMMIT_TS OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE _ts_rc)
if(NOT _ts_rc EQUAL 0 OR NOT FWOG_GIT_COMMIT_TS MATCHES "^[0-9]+$")
    set(FWOG_GIT_COMMIT_TS 0)
endif()

add_subdirectory(wiliOGbsp/bsp)

# ---- display CPU ----
add_executable(wilidoro_display
    src/target_og/main.c
)
target_include_directories(wilidoro_display PRIVATE
    src/core src/hal src/app src/ui src/target_og
    config
)
target_link_libraries(wilidoro_display PRIVATE
    pico_stdlib hardware_clocks hardware_gpio hardware_spi hardware_dma
    hardware_irq hardware_pwm hardware_i2c hardware_pio
    fwog_display_bsp
)
fwog_display_app(wilidoro_display
    VERSION 001
    DESCRIPTION "Pomodoro timer: themed countdown, LED progress ring, chimes and tilt-to-pause")

# ---- main CPU ----
# In Plan A this app exists to carry the display image onto the board: a
# display app cannot be UF2-flashed directly. Plan OG-D gives it the radio.
set(FWOG_DISPLAY_FIRMWARE "wilidoro_display" CACHE STRING "Display image to embed" FORCE)

add_executable(wilidoro_main
    src/main_og/main.c
)
target_include_directories(wilidoro_main PRIVATE
    src/core src/hal src/app src/main_og
)
target_link_libraries(wilidoro_main PRIVATE
    pico_stdlib hardware_clocks hardware_gpio hardware_spi hardware_pio
    fwog_main_bsp
)
# Generate and link the embedded display blob. fwog_main_app() does NOT do this,
# and fwog_display_update_run() references three symbols only the generated blob
# defines -- without this call the main app fails to link. Must sit between
# target_link_libraries() and fwog_main_app(); see the BSP's own
# apps/template_main/CMakeLists.txt for the reference ordering.
fwog_embed_display_image(wilidoro_main)
fwog_main_app(wilidoro_main
    VERSION 001
    DESCRIPTION "Pomodoro timer, main CPU: carries the display image and (from Plan OG-D) the sub-GHz beacon")
```

- [ ] **Step 6: Write the two skeleton apps**

`src/target_og/main.c`:

```c
/* FreeWili OG display CPU: wilidoro entry point.
 *
 * Plan A brings the board up and proves the flash path. LVGL arrives in
 * Task 4; the app loop in Task 5. */
#include "fwog_display.h"
#include "pico/stdlib.h"

/* Red held 6 s powers the board off, countdown on the WS2812 bar. board_init()
   references a symbol only this macro defines -- an app declaring no policy
   does not link. */
FWOG_POWER_DEFAULT();

int main(void) {
    board_init();

    /* Poll fast, report slowly: sampling buttons once a second is too coarse
       for the debouncer the ship-mode hold is built on. */
    absolute_time_t next_beat = make_timeout_time_ms(1000);
    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        fwog_power_poll(now);

        if (time_reached(next_beat)) {
            next_beat = make_timeout_time_ms(1000);
            DIAG("[wilidoro] display alive\n");
        }
        sleep_ms(2);
    }
}
```

`src/main_og/main.c`:

```c
/* FreeWili OG main CPU: wilidoro's radio half.
 *
 * In Plan A this exists for one reason -- fwog_display_update_run() is how the
 * display application reaches the display CPU, which has no BOOTSEL button and
 * must never be UF2-flashed directly. Plan OG-D adds the CC1101 beacon. */
#include "fwog_main.h"
#include "pico/stdlib.h"

int main(void) {
    board_init();

    /* Push the embedded display image if the display CPU's copy differs
       (compared by image CRC32, so an unchanged image is skipped). This also
       performs board_release_display() itself, so we must not call it. */
    const fwog_display_result_t disp = fwog_display_update_run();

    /* Announce the result for the first 10 s rather than once: the handshake
       finishes before USB CDC has enumerated and the host has asserted DTR,
       and pico_stdio_usb DROPS anything written before then. */
    const absolute_time_t announce_until = make_timeout_time_ms(10000);

    absolute_time_t next_beat = make_timeout_time_ms(1000);
    while (true) {
        /* REQUIRED -- see Global Constraints. board_init() arms a 2 s watchdog
           and kicking it is the app's job. Omitting this builds and links
           fine, then resets the board every 2 s forever. */
        board_watchdog_kick();

        if (time_reached(next_beat)) {
            next_beat = make_timeout_time_ms(1000);
            if (!time_reached(announce_until)) {
                DIAG("[wilidoro] main alive, display: %s\n",
                     fwog_display_result_text(disp));
            } else {
                DIAG("[wilidoro] main alive\n");
            }
        }
        sleep_ms(2);
    }
}
```

- [ ] **Step 7: Add the OG preset**

Replace `CMakePresets.json`:

```json
{
  "version": 3,
  "configurePresets": [
    { "name": "target", "generator": "Ninja", "binaryDir": "${sourceDir}/build",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "RelWithDebInfo", "WILIDORO_BOARD": "fw2" } },
    { "name": "og", "generator": "Ninja", "binaryDir": "${sourceDir}/build-og",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "RelWithDebInfo", "WILIDORO_BOARD": "og" } }
  ],
  "buildPresets": [
    { "name": "target", "configurePreset": "target" },
    { "name": "og", "configurePreset": "og" }
  ]
}
```

Separate `binaryDir`s are required — one build tree cannot hold both boards.

- [ ] **Step 8: Write the OG build script**

`tools/build_og.ps1`:

```powershell
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$sdk  = "$env:USERPROFILE/.pico-sdk"
$SdkPath   = (Get-ChildItem "$sdk/sdk" | Sort-Object Name -Descending | Select-Object -First 1).FullName
$Toolchain = (Get-ChildItem "$sdk/toolchain" | Sort-Object Name -Descending | Select-Object -First 1).FullName
$PicotoolDir = (Get-ChildItem "$sdk/picotool" -Recurse -Filter "picotool" -Directory | Select-Object -First 1).FullName
if ($args -contains "-Clean") { Remove-Item "$root/build-og" -Recurse -Force -ErrorAction SilentlyContinue }
# NOTE: no -DPICO_BOARD here, deliberately. The root CMakeLists sets it; passing
# it on the command line silently reverts the board config.
cmake -G Ninja -B "$root/build-og" -S "$root" -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DWILIDORO_BOARD=og `
  -DPICO_SDK_PATH="$SdkPath" -DPICO_TOOLCHAIN_PATH="$Toolchain" `
  -DPICO_PLATFORM=rp2040 -Dpicotool_DIR="$PicotoolDir"
cmake --build "$root/build-og"
```

- [ ] **Step 9: Write the flash script, with the guard**

`tools/flash_og.ps1`:

`fw.py flash` cannot be used directly: it takes only a positional app name and
resolves the UF2 as `REPO_ROOT/build/apps/<app>/<app>.uf2`, where `REPO_ROOT`
is derived from `fw.py`'s own location — the submodule, not this repo. So we
use `fw.py bootsel --cpu main`, which *is* location-independent, and do the
copy ourselves.

```powershell
# Flash the FreeWili OG. ONLY the main app may be flashed.
#
# A display app's UF2 links at 0x10021000 and a raw copy writes only that --
# not the app metadata sector at 0x10020000, which only the serial update path
# writes. The bootloader's app_valid check then fails against stale metadata,
# the app never runs, and the display CPU (the one CPU with no BOOTSEL button)
# stops enumerating. The main app carries the display image over the link WITH
# its metadata, which is why this is the only supported route.
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$app  = if ($args.Count -ge 1) { $args[0] } else { "wilidoro_main" }

if ($app -like "*_display") {
    Write-Error @"
Refusing to flash '$app'.

Flashing a display application by UF2 takes the display CPU off USB and it has
no BOOTSEL button. Flash the main app instead -- it pushes the display image
over the inter-CPU link, with the metadata sector the bootloader needs:

    powershell -File tools/flash_og.ps1 wilidoro_main
"@
    exit 1
}

$uf2 = "$root/build-og/$app.uf2"
if (-not (Test-Path $uf2)) { Write-Error "not built: $uf2"; exit 1 }

# Reboot the MAIN CPU into BOOTSEL by opening its USB CDC port at 1200 baud.
# Identifying by CPU means only one RPI-RP2 volume ever appears -- both RP2040s
# present indistinguishable volumes, so two at once cannot be told apart.
python "$root/wiliOGbsp/tools/fw.py" bootsel --cpu main

$deadline = (Get-Date).AddSeconds(30)
do {
    Start-Sleep -Milliseconds 500
    $vols = @(Get-Volume | Where-Object { $_.DriveType -eq 'Removable' -and $_.DriveLetter } |
              Where-Object { Test-Path "$($_.DriveLetter):\INFO_UF2.TXT" })
} while ($vols.Count -eq 0 -and (Get-Date) -lt $deadline)

if ($vols.Count -eq 0) { Write-Error "no RPI-RP2 volume appeared within 30 s"; exit 1 }
if ($vols.Count -gt 1) {
    Write-Error "multiple RPI-RP2 volumes ($($vols.DriveLetter -join ', ')). Both RP2040s look identical; put only one in BOOTSEL."
    exit 1
}

Copy-Item $uf2 "$($vols[0].DriveLetter):\" -Force
Write-Host "flashed $app to $($vols[0].DriveLetter): -- the board reboots and pushes the display image"
```

- [ ] **Step 10: Confirm the display bootloader is on the board**

The serial bootloader is a once-per-board step and is a prerequisite for
anything in this plan reaching the display CPU. From the submodule:

Run: `python wiliOGbsp/tools/fw.py console --product "FWOG display"`
Expected: the display CPU enumerates and identifies as `FWOG display ...`.

`fw.py console` takes `--port`/`--product`, **not** `--cpu` — `--cpu` belongs
to `bootsel`. `do_console()` also reads stdin to EOF, so under a
non-interactive shell it exits immediately; that is expected, and enumeration
is what is being checked. A stronger signal is available without the console:
the main CPU's heartbeat reports `HB display=...`, the cached boot-time result
of `fwog_display_update_run()`. `display=app updated` proves the link came up,
the display bootloader handshook, accepted a full image and jumped to the app.

If it does not enumerate at all, flash the bootloader once — this is the one
display-CPU UF2 flash that is correct:
`cd wiliOGbsp && python tools/fw.py bootloader`

- [ ] **Step 11: Build both apps**

Run: `powershell -File tools/build_og.ps1`
Expected: `build-og/wilidoro_display.uf2` and `build-og/wilidoro_main.uf2`,
and a POST_BUILD `check_uf2_info.py` pass on each (proving the shims work and
the UF2 info records are present).

- [ ] **Step 12: Verify FW2 still builds from the restructured CMake**

Run: `powershell -File tools/build.ps1 -Clean`
Expected: `build/wilidoro.uf2`, unchanged behaviour.

- [ ] **Step 13: Flash and confirm both CPUs are alive**

Run: `powershell -File tools/flash_og.ps1`
Then: `python wiliOGbsp/tools/fw.py console --product "FWOG display"`
Expected: `[wilidoro] display alive` once a second. The main CPU's console
(`--product "FWOG main"`) shows `[wilidoro] main alive`.

Also confirm the guard fires:
Run: `powershell -File tools/flash_og.ps1 wilidoro_display`
Expected: refuses with the explanation, exit code 1, nothing flashed.

- [ ] **Step 14: Commit**

```bash
git add .gitmodules wiliOGbsp CMakeLists.txt CMakePresets.json cmake/ \
        src/target_og/main.c src/main_og/main.c \
        tools/build_og.ps1 tools/flash_og.ps1 \
        tools/gen_uf2_info.py tools/check_uf2_info.py tools/genimage.py
git commit -m "build: add the FreeWili OG as a second board target

One repo, two boards, selected by -DWILIDORO_BOARD=fw2|og -- PICO_BOARD is a
single global cache value and the two boards are different silicon, so one
configure cannot build both.

The OG builds two executables because it has two CPUs. Only the main app may
be flashed; flash_og.ps1 refuses a display app, which would take the display
CPU off USB with no BOOTSEL button to recover it.

tools/*.py are shims: the BSP resolves its Python helpers against
CMAKE_SOURCE_DIR, which is this repo rather than the submodule."
```

---

### Task 3: Buttons and the power policy through the HAL

**Files:**
- Create: `src/hal/hal_og.c`
- Modify: `cmake/board_og.cmake` (add `src/hal/hal_og.c` to `wilidoro_display`)
- Modify: `src/target_og/main.c`

**Interfaces:**
- Consumes: `hal_led_count()` from Task 1; the `wilidoro_display` target from Task 2.
- Produces: the full `hal.h` implementation for the OG. Task 4 calls
  `hal_init()`/`hal_pump()`/`hal_now_ms()` from the LVGL tick and input hooks;
  Task 5 calls `hal_next_button()`.

- [ ] **Step 1: Write `hal_og.c` with buttons, time and capabilities**

Everything else in the seam is stubbed honestly — Plan OG-B fills LEDs, audio
and the IMU; Plan OG-D fills the beacon.

```c
/* FreeWili OG display-CPU HAL backend.
 *
 * The OG's five coloured buttons map 1:1 onto the softkey columns the app
 * already routes (grey..red == cols 0..4). The FreeWili 2's D-pad, HOME, OK,
 * CANCEL and PAGE have no counterpart and are never emitted; the screens
 * spend a softkey column on Back instead. */
#include "hal.h"
#include "fwog_display.h"
#include "pico/stdlib.h"
#include <string.h>

#define BTN_QUEUE_LEN 8
static hal_btn_t s_queue[BTN_QUEUE_LEN];
static uint8_t   s_head, s_tail;
static bool      s_power_armed;

static void queue_push(hal_btn_t b) {
    uint8_t next = (uint8_t)((s_head + 1u) % BTN_QUEUE_LEN);
    if (next == s_tail) return;          /* full: drop, never overwrite */
    s_queue[s_head] = b;
    s_head = next;
}

void hal_init(void) {
    s_head = s_tail = 0;
    s_power_armed = false;
    /* board_init() is called by main() before hal_init(); it owns the panel,
       the I2C bus, the buttons and the backlight PWM divider. */
    hal_backlight(100);
}

/* Once per main-loop iteration. fwog_power_poll() is THE button read: calling
   fwog_buttons_poll() again here would consume edges out from under the
   ship-mode hold machine, which depends on that debounce state. */
void hal_pump(void) {
    const fwog_power_t p = fwog_power_poll(hal_now_ms());
    s_power_armed = p.armed;

    /* fwog_btn_id_t is GRAY,YELLOW,GREEN,BLUE,RED == 0..4, and hal_btn_t is
       GREY,YELLOW,GREEN,BLUE,RED == 0..4. Same order, same colours. */
    for (unsigned i = 0; i < FWOG_BTN_COUNT; i++) {
        if (p.buttons.pressed & (1u << i)) queue_push((hal_btn_t)i);
    }
}

uint32_t hal_now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

bool hal_next_button(hal_btn_t *out) {
    if (s_tail == s_head) return false;
    *out = s_queue[s_tail];
    s_tail = (uint8_t)((s_tail + 1u) % BTN_QUEUE_LEN);
    return true;
}

/* True while a shutdown countdown is running. fwog_power_poll() paints that
   countdown onto the WS2812 bar itself and restores the previous colours if
   the hold is aborted, so the app must not write LEDs during it. */
bool hal_power_armed(void) { return s_power_armed; }

/* ---- LEDs: Plan OG-B ---- */
int  hal_led_count(void) { return FWOG_LED_COUNT; }   /* 7 */
void hal_led_set(int i, uint8_t r, uint8_t g, uint8_t b) { (void)i;(void)r;(void)g;(void)b; }
void hal_led_brightness(uint8_t level) { (void)level; }
void hal_led_show(void) { }

/* ---- Audio: Plan OG-B ---- */
void hal_tone(uint16_t hz, uint16_t ms, uint8_t amp) { (void)hz;(void)ms;(void)amp; }
void hal_audio_idle(void) { }

/* ---- Backlight: no light sensor on this board, so this is a fixed level ---- */
void hal_backlight(uint8_t pct) { board_backlight(pct); }

/* ---- Absent hardware ---- */
bool hal_dvi_surface(hal_dvi_surface_t *s) { (void)s; return false; }   /* no HSTX on RP2040 */
void hal_dvi_enable(bool on) { (void)on; }
bool hal_lux(float *lux) { (void)lux; return false; }                   /* no ambient sensor */

/* ---- IMU: Plan OG-B ---- */
bool hal_imu(float *ax, float *ay, float *az) { (void)ax;(void)ay;(void)az; return false; }

/* ---- Beacon: Plan OG-D, over the inter-CPU link ---- */
void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN]) { (void)wire; }
bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]) { (void)wire; return false; }

hal_caps_t hal_caps(void) {
    hal_caps_t c = {0};
    c.buttons = true;
    c.leds    = false;   /* Plan OG-B */
    c.audio   = false;   /* Plan OG-B */
    c.imu     = false;   /* Plan OG-B */
    c.radio   = false;   /* Plan OG-D */
    c.light   = false;   /* no hardware: backlight is fixed */
    c.dvi     = false;   /* no hardware: RP2040 has no HSTX */
    return c;
}
```

- [ ] **Step 2: Declare `hal_power_armed()` in the seam**

`src/hal/hal.h`, after `hal_next_button`:

```c
/* True while a hardware power-off countdown is running (FreeWili OG: red held).
   The BSP paints that countdown on the LED bar itself, so the app must skip
   its own LED writes while this is true. Always false on FreeWili 2. */
bool hal_power_armed(void);
```

Add `bool hal_power_armed(void) { return false; }` to both `src/hal/hal_target.c`
and `src/hal/hal_sim.c`.

- [ ] **Step 3: Add the HAL to the OG build**

In `cmake/board_og.cmake`, the `wilidoro_display` source list becomes:

```cmake
add_executable(wilidoro_display
    src/target_og/main.c
    src/hal/hal_og.c
)
```

- [ ] **Step 4: Drive it from the display main loop**

Replace the loop body in `src/target_og/main.c`:

```c
int main(void) {
    board_init();
    hal_init();

    absolute_time_t next_beat = make_timeout_time_ms(1000);
    while (true) {
        hal_pump();                      /* calls fwog_power_poll() exactly once */

        hal_btn_t b;
        while (hal_next_button(&b)) {
            DIAG("[wilidoro] button %d\n", (int)b);
        }

        if (time_reached(next_beat)) {
            next_beat = make_timeout_time_ms(1000);
            DIAG("[wilidoro] display alive\n");
        }
        sleep_ms(2);
    }
}
```

Remove the now-unused `fwog_power_poll()` call and the `now` variable from
`main()` — `hal_pump()` owns that call, and calling it twice per iteration is
exactly the bug the BSP warns about.

- [ ] **Step 5: Build and flash**

Run: `powershell -File tools/build_og.ps1` then `powershell -File tools/flash_og.ps1`
Expected: both build; the main app carries the new display image over.

- [ ] **Step 6: Verify on hardware**

Run: `python wiliOGbsp/tools/fw.py console --product "FWOG display"`

(`console` takes `--port`/`--product`; `--cpu` belongs to `bootsel`. It also
reads stdin to EOF, so it exits immediately under a non-interactive shell —
run it from an interactive terminal to watch the button lines arrive.)

Press each of the five buttons in turn. Expected: `button 0` for grey through
`button 4` for red, one line per press with no repeats or missed edges.

Then hold red for 6 seconds. Expected: the WS2812 bar shows the countdown and
the board powers off. Press-and-release red short: a single `button 4`, no
countdown, no power-off.

- [ ] **Step 7: Verify FW2 still builds and tests pass**

Run: `powershell -File tools/test.ps1` then `powershell -File tools/build.ps1`
Expected: both green — `hal_power_armed()` added to all three backends.

- [ ] **Step 8: Commit**

```bash
git add src/hal/hal_og.c src/hal/hal.h src/hal/hal_target.c src/hal/hal_sim.c \
        src/target_og/main.c cmake/board_og.cmake
git commit -m "feat(og): HAL backend with buttons and the power policy

The OG's five coloured buttons map 1:1 onto the softkey columns the app
already routes. hal_pump() makes fwog_power_poll() the single button read
per iteration -- a second fwog_buttons_poll() would consume edges out from
under the ship-mode hold machine.

Absent hardware is reported through hal_caps rather than faked: no ambient
sensor, no HSTX. LEDs, audio, IMU and the beacon are stubbed for later plans."
```

---

### Task 4: LVGL on the ST7789, and the RAM number

The spec calls for measuring the RAM budget first, not last. This task ends
with a real number for how much SRAM is left.

**Files:**
- Create: `src/target_og/lvgl_port_og.c`, `src/target_og/lvgl_port_og.h`
- Modify: `config/lv_conf.h`, `cmake/board_og.cmake`, `src/target_og/main.c`

**Interfaces:**
- Consumes: `hal_now_ms()` from Task 3.
- Produces: `void lvgl_port_og_init(void);` and `#define DISP_HOR 320`,
  `#define DISP_VER 240` in `lvgl_port_og.h`. Task 5 calls
  `lvgl_port_og_init()` before building screens, and `lv_timer_handler()`
  from the main loop.

- [ ] **Step 1: Enable Montserrat 40 for the OG**

In `config/lv_conf.h`, line ~500, `LV_FONT_MONTSERRAT_40` is currently `0`. The
FW2 face uses 48, which does not fit 320 px. Make both available:

```c
#define LV_FONT_MONTSERRAT_40 1   /*large timer digits (FreeWili OG, 320x240)*/
```

Leave `LV_FONT_MONTSERRAT_48` at `1`. Both are flash-resident and the OG has
16 MB, so carrying both costs nothing that matters.

- [ ] **Step 2: Write the port header**

`src/target_og/lvgl_port_og.h`:

```c
#ifndef WILIDORO_LVGL_PORT_OG_H
#define WILIDORO_LVGL_PORT_OG_H

/* ST7789 panel geometry. The FreeWili 2's ST7796 is 480x320. */
#define DISP_HOR 320
#define DISP_VER 240

void lvgl_port_og_init(void);
#endif
```

- [ ] **Step 3: Write the port**

`src/target_og/lvgl_port_og.c`:

```c
/* LVGL 9 <-> ST7789 on the FreeWili OG display CPU.
 *
 * Partial render mode with two 320x40 buffers. The flush is blocking, as on
 * the FreeWili 2: the panel shares SPI1, so exactly one bus owner at a time
 * is the simplest correct arrangement. */
#include "lvgl_port_og.h"
#include "lvgl.h"
#include "fwog_display.h"
#include "hal.h"

#define BUF_LINES 40
#define BUF_PX    (DISP_HOR * BUF_LINES)

static lv_color_t s_buf1[BUF_PX];
static lv_color_t s_buf2[BUF_PX];

static uint32_t tick_cb(void) { return hal_now_ms(); }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    const uint16_t x = (uint16_t)area->x1;
    const uint16_t y = (uint16_t)area->y1;
    const uint16_t w = (uint16_t)(area->x2 - area->x1 + 1);
    const uint16_t h = (uint16_t)(area->y2 - area->y1 + 1);

    st7789_dma_wait();                 /* previous blit must be retired */
    st7789_set_window(x, y, w, h);
    st7789_blit((const uint16_t *)px, (size_t)w * (size_t)h);
    st7789_dma_wait();

    lv_display_flush_ready(disp);
}

void lvgl_port_og_init(void) {
    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *disp = lv_display_create(DISP_HOR, DISP_VER);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, s_buf1, s_buf2, sizeof(s_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
}
```

**If the panel shows swapped colours**, the ST7789 wants byte-swapped RGB565
like the FW2's ST7796 does. In that case add
`lv_draw_sw_rgb565_swap(px, (uint32_t)w * h);` immediately before
`st7789_set_window()`, and note it in `docs/hardware-notes.md`. Check
`st7789_rgb565()` in the BSP first — if it packs plainly, try without the swap
and only add it if the colours are visibly wrong.

- [ ] **Step 4: Add to the build**

`cmake/board_og.cmake` — source list and link line:

```cmake
add_executable(wilidoro_display
    src/target_og/main.c
    src/target_og/lvgl_port_og.c
    src/hal/hal_og.c
)
```

and add `lvgl` to `target_link_libraries(wilidoro_display PRIVATE ...)`.

- [ ] **Step 5: Draw something and pump LVGL**

In `src/target_og/main.c`, add `#include "lvgl.h"` and
`#include "lvgl_port_og.h"`, then after `hal_init()`:

```c
    lvgl_port_og_init();

    /* A label proves the panel, the flush path and the font all work. */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101014), 0);
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "25:00");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF5B45), 0);
    lv_obj_center(lbl);
```

and inside the loop, before `sleep_ms(2)`:

```c
        lv_timer_handler();
```

- [ ] **Step 6: Build, flash, and read the RAM number**

Run: `powershell -File tools/build_og.ps1`

The link step prints region usage. Record the RAM figure. Expected: roughly
110–135 KB used of 264 KB, consistent with the spec's estimate (51 KB buffers +
48 KB LVGL heap + app/stacks).

If RAM is over ~200 KB, stop and reduce `LV_MEM_SIZE` in `config/lv_conf.h`
for the OG before continuing — Task 5 and Plan OG-C build three themes on top
of this budget, and discovering the ceiling then is far more expensive.

Run: `powershell -File tools/flash_og.ps1`
Expected: `25:00` centred on the panel in warm orange on near-black.

- [ ] **Step 7: Record the measurement**

Append to `docs/hardware-notes.md` a short OG section stating the measured
flash and RAM usage of `wilidoro_display` with LVGL and one label, and whether
the RGB565 byte swap was needed. This is the baseline every later OG task is
judged against.

- [ ] **Step 8: Verify FW2 is unaffected**

Run: `powershell -File tools/build.ps1`
Expected: green. `LV_FONT_MONTSERRAT_40 1` adds a font to the FW2 image too;
confirm the FW2 build still fits flash (it has 16 MB, so this is a formality).

- [ ] **Step 9: Commit**

```bash
git add src/target_og/lvgl_port_og.c src/target_og/lvgl_port_og.h \
        src/target_og/main.c cmake/board_og.cmake config/lv_conf.h \
        docs/hardware-notes.md
git commit -m "feat(og): LVGL 9 on the ST7789

Partial render mode, two 320x40 buffers, blocking flush -- the panel shares
SPI1, so one bus owner at a time is the simplest correct arrangement.

Records the measured RAM baseline in hardware-notes: three themes get built
on top of this budget, so the ceiling is worth knowing now rather than later."
```

---

### Task 5: The timer screen and the neon theme at 320×240

Ends Plan A with a real, running pomodoro on the OG: a countdown that starts,
pauses and resets from the buttons.

**Files:**
- Modify: `src/ui/screen_timer.c`, `src/ui/theme_neon.c` (320×240 geometry)
- Modify: `cmake/board_og.cmake`
- Modify: `src/target_og/main.c`

**Interfaces:**
- Consumes: `lvgl_port_og_init()` (Task 4); `hal_next_button()`, `hal_caps()`
  (Task 3); `led_pattern_render(..., count)` (Task 1).
- Produces: a running app loop on the OG. Plan OG-C adds the arcade and flip
  themes against the same geometry macros this task introduces.

- [ ] **Step 1: Introduce board-conditional layout geometry**

The screens and themes are shared source. Rather than fork them, give them
named geometry. In `src/ui/ui.h`, add near the top:

```c
/* Panel geometry and the type scale that follows from it. The FreeWili 2's
   ST7796 is 480x320; the OG's ST7789 is 320x240. Screens and themes lay out
   against these rather than literals, so one set of sources serves both. */
#if defined(WILIDORO_BOARD_OG)
  #define UI_W               320
  #define UI_H               240
  #define UI_FONT_BIG        (&lv_font_montserrat_40)
  #define UI_SOFTKEY_H       28
  #define UI_SOFTKEY_BTN_W   60
  #define UI_SOFTKEY_BTN_H   22
#else
  #define UI_W               480
  #define UI_H               320
  #define UI_FONT_BIG        (&lv_font_montserrat_48)
  #define UI_SOFTKEY_H       34
  #define UI_SOFTKEY_BTN_W   92
  #define UI_SOFTKEY_BTN_H   28
#endif
/* The face is everything above the softkey bar. */
#define UI_FACE_H           (UI_H - UI_SOFTKEY_H)
```

The FW2 values are the literals currently in the source (`480, 34` for the bar,
`92, 28` for a button, `480, 286` for the face), so the FW2 build is unchanged
by construction. The OG values are those proportions at 320×240: 320/5 = 64 per
column less a 4 px gutter.

Define `WILIDORO_BOARD_OG` from CMake — in `cmake/board_og.cmake`, after the
`add_executable(wilidoro_display ...)` call:

```cmake
target_compile_definitions(wilidoro_display PRIVATE WILIDORO_BOARD_OG=1)
```

`UI_W`/`UI_H` and Task 4's `DISP_HOR`/`DISP_VER` now both encode 320×240 —
two sources of truth that could silently diverge, with a corrupted display as
the symptom. Pin them together: add to `src/target_og/lvgl_port_og.c`, after
its includes,

```c
#include "ui.h"
_Static_assert(DISP_HOR == UI_W && DISP_VER == UI_H,
               "LVGL panel geometry and the UI layout geometry disagree");
```

- [ ] **Step 2: Replace the hardcoded geometry — four lines, exactly**

The geometry surface is small. These are all of it; change nothing else, so
that FW2 renders identically to before.

`src/ui/ui.c:22` — the softkey bar:

```c
    lv_obj_set_size(bar, UI_W, UI_SOFTKEY_H);
```

`src/ui/ui.c:30` — one softkey button:

```c
        lv_obj_set_size(btn, UI_SOFTKEY_BTN_W, UI_SOFTKEY_BTN_H);
```

`src/ui/screen_timer.c:24` — the face container:

```c
    lv_obj_set_size(s_face, UI_W, UI_FACE_H);
```

`src/ui/theme_neon.c:31` — the large countdown font:

```c
    lv_obj_set_style_text_font(s_time, UI_FONT_BIG, 0);
```

Leave `lv_font_montserrat_16` on line 37 alone — the small state label reads
fine at both sizes.

Confirm nothing was missed:

Run: `grep -nE "480|, 320|montserrat_48" src/ui/*.c`
Expected: no matches outside comments.

- [ ] **Step 3: Verify FW2 is pixel-unchanged**

Run: `powershell -File tools/build.ps1` and flash a FreeWili 2 if one is to hand.
Expected: the neon timer face looks exactly as it did. If a FreeWili 2 is not
available, run the simulator: `powershell -File tools/sim.ps1`.

This is the check that the geometry refactor was mechanical.

- [ ] **Step 4: Add the app and UI sources to the OG target**

`cmake/board_og.cmake` — `wilidoro_display` gains the shared app, core and UI
sources. Note `dvi_view.c` is absent: it is FW2-only.

```cmake
add_executable(wilidoro_display
    src/target_og/main.c
    src/target_og/lvgl_port_og.c
    src/hal/hal_og.c
    src/core/pomodoro.c
    src/core/beacon.c
    src/core/beacon_rx.c
    src/core/tilt.c
    src/core/dimming.c
    src/app/app_model.c
    src/app/app.c
    src/app/timer_view.c
    src/app/led_pattern.c
    src/app/sound.c
    src/ui/ui.c
    src/ui/screen_timer.c
    src/ui/theme.c
    src/ui/theme_neon.c
)
```

`src/ui/theme.c` returns three themes; `theme_arcade.c` and `theme_flip.c`
arrive in Plan OG-C. Until then, make `theme_get()` clamp to the neon theme
when the others are not linked, by guarding their `extern` descriptors:

In `src/ui/theme.c`, wrap the registry:

```c
#if defined(WILIDORO_BOARD_OG)
/* Plan OG-C links the other two. Until then every index resolves to neon. */
const theme_t *theme_get(uint8_t idx) { (void)idx; return &THEME_NEON; }
#else
/* ... existing three-entry registry unchanged ... */
#endif
```

`screen_settings.c` and `screen_nearby.c` are also deferred to Plan OG-C, so
`app.c` must not reference them on the OG. Four sites need guarding with
`#if !defined(WILIDORO_BOARD_OG)`:

- **`app.c:5-7`** — the `#include` of `screen_settings.h` and `screen_nearby.h`
  (keep `screen_timer.h` unguarded).
- **`app.c:83-88`** — in `route_softkey()`, the `SCREEN_SETTINGS` and
  `SCREEN_NEARBY` switch cases.
- **`app.c:224-227`** — in the refresh switch, the same two cases.
- **`app.c:251-254`** — in `app_init()`, the two
  `s_scr[...] = screen_*_create();` lines.

`app_goto()` at 231-233 needs no guard: on the OG nothing ever calls it with
anything but `SCREEN_TIMER`, and `s_app.screen` is initialised to
`SCREEN_TIMER` at 247.

Also guard the Nearby/Settings entries in whatever softkey label table
`screen_timer.c` uses to reach them, so the OG's timer face does not offer a
softkey that goes nowhere. Grep for `SCREEN_SETTINGS` in `src/ui/` to find it.

- [ ] **Step 5: Run the app from the OG main loop**

Replace the body of `src/target_og/main.c`'s `main()` — the standalone label
from Task 4 goes away, `app_init()` owns the screen now:

There is **no** `app_tick()`. `app_init()` starts an LVGL timer that drives the
app, so the loop only pumps the HAL and LVGL — this mirrors `src/target/main.c`
exactly. Add `#include "app.h"` at the top.

**Do NOT drop the ST7789 init block.** Task 4 added it after a hardware failure:
without it `st7789_ready()` is false forever and every flush silently no-ops,
leaving the bootloader's UI on the panel. Keep it exactly where it is, before
`lvgl_port_og_init()`. Only the label block and the heartbeat go away.

```c
int main(void) {
    board_init();
    hal_init();

    /* Bounded, not unbounded. st7789_init_begin() can fail to leave
       ST7789_INIT_IDLE when clk_peri cannot reach the panel rate, which makes
       st7789_init_step() a permanent no-op -- and this CPU has no watchdog to
       recover a spin. Do not remove the deadline. */
    st7789_init_begin();
    const absolute_time_t lcd_deadline = make_timeout_time_ms(500);
    while (!st7789_ready() && !time_reached(lcd_deadline)) st7789_init_step();
    if (!st7789_ready()) {
        DIAG("[wilidoro] st7789 init FAILED\n");
    } else {
        st7789_clear(0x0000u);
        st7789_dma_wait();
    }

    lvgl_port_og_init();
    app_init();               /* builds screens, starts the tick timer */
    lv_timer_handler();       /* first frame */
    DIAG("wilidoro OG up: sys=%u kHz\n", (unsigned)(clock_get_hz(clk_sys) / 1000u));

    for (;;) {
        hal_pump();           /* the one fwog_power_poll() per iteration */
        lv_timer_handler();
        sleep_ms(2);
    }
}
```

`clock_get_hz` needs `#include "hardware/clocks.h"`. Drop the Task 4 label
block and the heartbeat `DIAG` — `app_init()` owns the screen now.

- [ ] **Step 6: Build and flash**

Run: `powershell -File tools/build_og.ps1` then `powershell -File tools/flash_og.ps1`

- [ ] **Step 7: Verify on hardware**

Expected on the panel: the neon timer face at 320×240, reading `25:00` idle.

- Press the softkey that starts a session — the countdown runs, one second per second.
- Press pause — it stops; press again — it resumes.
- Press reset — back to `25:00`.
- The LED ring stays dark and the chime is silent (Plan OG-B). `hal_caps()`
  correctly reports the light, DVI, radio, audio, IMU and LED capabilities as
  false, but nothing on the timer face surfaces that yet -- `hal_caps()`'s
  only reader is `screen_settings.c`, which is compiled out on the OG.
  Surfacing capability gaps on the timer face itself is later-plan work.

Confirm the softkey labels match what the buttons actually do, given red is
also the power button.

- [ ] **Step 8: Run the host tests**

Run: `powershell -File tools/test.ps1`
Expected: all green — this task changed UI geometry only, and `src/core/` and
`src/app/` logic is untouched.

- [ ] **Step 9: Commit**

```bash
git add src/ui/ui.h src/ui/screen_timer.c src/ui/theme_neon.c src/ui/theme.c \
        src/app/app.c cmake/board_og.cmake src/target_og/main.c
git commit -m "feat(og): running pomodoro with the neon theme at 320x240

Screens and themes lay out against UI_W/UI_H/UI_FONT_BIG rather than
literals, so one set of sources serves both panels and the FreeWili 2
rendering is unchanged.

Settings, Nearby and the other two themes are guarded out until Plan OG-C."
```

---

## What Plan A deliberately leaves undone

Each becomes its own plan, in this order:

- **Plan OG-B — Peripherals.** LEDs (7, and not fighting the power countdown),
  audio (8 kHz I²S tone synthesis with the ≥2.5 kHz clamp and its host tests),
  tilt-to-pause (LIS3DH, including the bench-measured axis orientation).
- **Plan OG-C — Themes, screens and the simulator.** Arcade and flip themes at
  320×240, Settings and Nearby with their Back softkeys, and the SDL sim's OG
  geometry mode.
- **Plan OG-D — The beacon.** Link opcodes `0x40`/`0x41` and their host tests,
  the main-CPU CC1101 app, and the two-radio over-the-air self-test.

---

## Self-review notes

Checked against `docs/superpowers/specs/2026-07-28-wilidoro-og-port-design.md`:

- Spec items covered by this plan: board-target build system, submodule, flash
  guard, tooling, `hal_og.c` skeleton with honest `hal_caps`, LVGL port, RAM
  measurement, Montserrat 40, `led_pattern` count parameter with tests at both
  7 and 16, softkeys 1:1, no dimming (`light=false`, backlight fixed at 100).
- Spec items explicitly deferred, with a named plan: audio band-limiting, LED
  driver, IMU axis constant, remaining themes, Settings/Nearby Back softkeys,
  sim OG mode, beacon opcodes and the main-CPU radio app.
- One integration constraint was discovered during planning and is not in the
  spec — the BSP resolving its Python helpers against `CMAKE_SOURCE_DIR`.
  Task 2 Step 2 handles it; the spec should be amended if it is revised.
