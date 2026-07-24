# Wilidoro Plan A — Foundation & Core Logic — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up the wilidoro repo (build system, LVGL, host-test harness, SDL simulator) and implement the entire pure-logic `core/` (pomodoro state machine, beacon codec, gesture classifier, dimming curve) under TDD — ending with green host tests plus a blank LVGL screen rendering on both the Windows simulator and the FreeWili 2 hardware.

**Architecture:** A standalone C repo consuming `wilibsp` and `lvgl` as git submodules. Three build outputs: the RP2350B firmware (XIP-from-flash, LVGL partial buffers, wilibsp drivers), a Windows SDL simulator (LVGL's built-in SDL driver + stub HAL), and a host CTest binary set for `core/`. `core/` is pure C with zero hardware or LVGL dependencies so it is fully unit-testable off-target.

**Tech Stack:** C11, Pico SDK 2.2.0 (ARM GCC 14.2), LVGL 9.2.2, CMake + Ninja, wilibsp BSP, SDL2 (simulator, MSYS2 mingw64 gcc), CTest.

## Global Constraints

Every task's requirements implicitly include these. Values are copied verbatim from the research of `freewili/wilibsp` @ master and `freewili/sensorview` @ main.

- **Target MCU:** RP2350B (48 GPIO). `PICO_BOARD` is set to `freewili2` in CMake ONLY — never pass `-DPICO_BOARD` on the command line (forces RP2350A and breaks the build).
- **Clock:** use wilibsp `board_init()` as-is (vreg 1.25 V → 250 MHz → clk_peri re-sourced from clk_sys). Do NOT substitute sensorview's 153.6 MHz recipe — wilibsp audio derives MCLK = clk_sys/61 from 250 MHz.
- **Binary type:** `pico_set_binary_type(wilidoro default)` (XIP from 16 MB flash). LVGL overflows the 512 KB SRAM under `copy_to_ram`. Requires a flash-boot board header (`PICO_BOOT_STAGE2_CHOOSE_W25Q080`, `PICO_FLASH_SIZE_BYTES 16MB`).
- **Display:** ST7796 480×320, SPI1. Pixels are **big-endian (byte-swapped) RGB565** on the wire — swap with `lv_draw_sw_rgb565_swap` in the LVGL flush_cb.
- **LVGL buffers:** partial mode, 2× static SRAM buffers of 480×40 `lv_color_t` (38,400 B each). No full framebuffer.
- **Diagnostics:** SEGGER RTT only (`DIAG(...)`), no USB/UART stdio, **no floats and no `%f`** in DIAG format strings.
- **LEDs:** exactly 16 WS2812 pixels; data line is hardware-inverted (driver handles it); first `ws2812_show()` after PIO start only latches pixel 0 — re-show periodically.
- **Audio:** speaker is 0.5 W — keep levels bounded and power the codec down when idle (`codec_nau88c10_speaker_low_power()`).
- **Shared resources:** SPI1 is shared LCD+CC1101 (arbiter: spin on `!st7796_flush_busy()` before manual SPI); DMA_IRQ_0 is shared (use `irq_add_shared_handler`, never exclusive); PIO map is pio0=I2S, pio1=WS2812, pio2=radio/IR (if combining radio+IR, `gdo_capture_init()` must run before `ir_*_init()`).
- **Radio:** CC1101 has NO packet/FIFO engine. TX = `ook_tx_send(durs,n,start_level)` (µs durations, `OOK_TX_MAX_US 100000`); RX = `gdo_capture_*` PIO edge capture. Select antenna via `ioexp_antenna(ANT_CC1101_433)`.
- **`core/` purity:** files under `src/core/` include only the C standard library and their own headers — no LVGL, no Pico SDK, no wilibsp. Time is always passed in as a `uint32_t now_ms` parameter; `core/` never reads a clock.
- **Commit discipline:** every task ends on a green build/test and a commit. Commit message trailer:
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

---

## File Structure

```
wilidoro/
  wilibsp/                         submodule -> github.com/freewili/wilibsp
  third_party/lvgl/                submodule -> github.com/lvgl/lvgl @ v9.2.2
  boards/freewili2.h               our SDK board header (flash-boot settings)
  config/lv_conf.h                 shared LVGL config (device + sim via #ifdef)
  src/
    core/
      pomodoro.h  pomodoro.c       session state machine (pure)
      beacon.h    beacon.c         packet pack/unpack + CRC16 + OOK Manchester codec (pure)
      gestures.h  gestures.c       IMU gesture classifier (pure)
      dimming.h   dimming.c        lux -> backlight% + LED brightness curve (pure)
    hal/
      hal.h                        the interface every screen/feature calls (Plan B fills it)
    target/
      main.c                       RP2350B entry: board+LVGL bring-up, blank screen
      lvgl_port.c  lvgl_port.h     st7796 flush_cb + ft6336 indev + tick
      bl_pwm.c     bl_pwm.h        GPIO25 PWM backlight (auto-dim actuator)
    sim/
      main.c                       host entry: LVGL SDL window, blank screen
  tests/
    CMakeLists.txt                 host CTest project (no Pico SDK)
    test_pomodoro.c test_beacon.c test_gestures.c test_dimming.c
    greatest.h                     tiny header-only test framework (vendored)
  tools/
    build.ps1  flash.ps1  rtt.ps1  test.ps1  sim.ps1
  CMakeLists.txt                   device build (top-level)
  CMakePresets.json
  pico_sdk_import.cmake            copied from wilibsp
  .gitignore  .gitmodules
  docs/superpowers/{specs,plans}/
```

---

### Task 1: Repo scaffolding & submodules

**Files:**
- Create: `.gitignore` (already exists — extend), `.gitmodules` (via git), `pico_sdk_import.cmake`
- Create: submodules `wilibsp/`, `third_party/lvgl/`

**Interfaces:**
- Produces: the `wilibsp/` and `third_party/lvgl/` trees other tasks include; `pico_sdk_import.cmake` at repo root.

- [ ] **Step 1: Add submodules pinned to known-good refs**

```bash
cd /c/~prj/Dropbox/vibeProjects/wilidoro
git submodule add https://github.com/freewili/wilibsp.git wilibsp
git submodule add https://github.com/lvgl/lvgl.git third_party/lvgl
git -C third_party/lvgl checkout v9.2.2
git -C wilibsp checkout master
git add .gitmodules wilibsp third_party/lvgl
```

- [ ] **Step 2: Copy the SDK importer and extend .gitignore**

```bash
cp wilibsp/pico_sdk_import.cmake ./pico_sdk_import.cmake
printf 'build/\nbuild-*/\nbuild-tests/\n.superpowers/\n*.uf2\n' >> .gitignore
```

- [ ] **Step 3: Verify submodules resolved**

Run: `git submodule status`
Expected: two lines, one per submodule, each with a commit SHA and the checked-out tag/branch (no leading `-`).

- [ ] **Step 4: Commit**

```bash
git add .gitignore .gitmodules wilibsp third_party/lvgl pico_sdk_import.cmake
git commit -m "chore: scaffold wilidoro repo with wilibsp + lvgl submodules"
```

---

### Task 2: Host test harness (proves `core/` is buildable off-target)

**Files:**
- Create: `tests/greatest.h` (vendored header-only test framework), `tests/CMakeLists.txt`, `tests/test_smoke.c`
- Create: `tools/test.ps1`

**Interfaces:**
- Produces: a CTest project pattern (`add_executable` linking a `test_*.c` + the `core/*.c` under test, `add_test`), and `tools/test.ps1` running configure+build+ctest into `build-tests/`.

- [ ] **Step 1: Vendor the greatest.h test framework**

Download `greatest.h` (public-domain, single header) from https://raw.githubusercontent.com/silentbicycle/greatest/release/greatest.h into `tests/greatest.h`.

Run: `curl -fsSL https://raw.githubusercontent.com/silentbicycle/greatest/release/greatest.h -o tests/greatest.h`
Expected: file exists, contains `#define GREATEST_H`.

- [ ] **Step 2: Write the smoke test**

`tests/test_smoke.c`:
```c
#include "greatest.h"

TEST harness_works(void) {
    ASSERT_EQ(2, 1 + 1);
    PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(harness_works);
    GREATEST_MAIN_END();
}
```

- [ ] **Step 3: Write the host CTest project**

`tests/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20)
project(wilidoro_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()

set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../src/core)

function(add_core_test name)
  add_executable(${name} ${name}.c ${ARGN})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CORE})
  target_link_libraries(${name} m)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_core_test(test_smoke)
```

- [ ] **Step 4: Write tools/test.ps1**

`tools/test.ps1`:
```powershell
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$gcc  = "C:/msys64/mingw64/bin/gcc.exe"
cmake -G Ninja -B "$root/build-tests" -S "$root/tests" -DCMAKE_C_COMPILER="$gcc"
cmake --build "$root/build-tests"
ctest --test-dir "$root/build-tests" --output-on-failure
```

- [ ] **Step 5: Run tests — verify green**

Run: `powershell -File tools/test.ps1`
Expected: `test_smoke` builds and `ctest` reports `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 6: Commit**

```bash
git add tests/greatest.h tests/CMakeLists.txt tests/test_smoke.c tools/test.ps1
git commit -m "test: add host CTest harness with greatest.h"
```

---

### Task 3: `core/pomodoro` — session state machine (TDD)

**Files:**
- Create: `src/core/pomodoro.h`, `src/core/pomodoro.c`, `tests/test_pomodoro.c`
- Modify: `tests/CMakeLists.txt` (add `add_core_test(test_pomodoro ${CORE}/pomodoro.c)`)

**Interfaces:**
- Consumes: nothing (pure).
- Produces (verbatim header other tasks/plans rely on):
```c
// src/core/pomodoro.h
#ifndef WILIDORO_POMODORO_H
#define WILIDORO_POMODORO_H
#include <stdint.h>
#include <stdbool.h>

typedef enum { PM_IDLE, PM_FOCUS, PM_BREAK_SHORT, PM_BREAK_LONG, PM_ALARM, PM_PAUSED } pm_state_t;
typedef enum { PM_EV_NONE, PM_EV_FOCUS_ENDED, PM_EV_BREAK_ENDED } pm_event_t;

typedef struct { uint16_t focus_min, short_min, long_min, long_every; } pm_config_t;
typedef struct { uint16_t completed; uint16_t streak; uint32_t focus_seconds; } pm_stats_t;

typedef struct {
    pm_config_t cfg;
    pm_state_t  state;
    pm_state_t  resume_state;   // state to return to from PM_PAUSED
    uint32_t    phase_end_ms;   // absolute now_ms at which the running phase ends
    uint32_t    paused_left_ms; // remaining ms captured at pause
    uint16_t    focus_count;    // completed focus sessions this cycle (for long-break cadence)
    bool        alarm_is_focus; // PM_ALARM came from focus-end (true) vs unused
    pm_stats_t  stats;
} pomodoro_t;

void       pomodoro_init(pomodoro_t *p, pm_config_t cfg);
void       pomodoro_start_focus(pomodoro_t *p, uint32_t now_ms);
void       pomodoro_pause(pomodoro_t *p, uint32_t now_ms);
void       pomodoro_resume(pomodoro_t *p, uint32_t now_ms);
void       pomodoro_skip(pomodoro_t *p, uint32_t now_ms);
void       pomodoro_add5(pomodoro_t *p, uint32_t now_ms);
void       pomodoro_acknowledge(pomodoro_t *p, uint32_t now_ms);
pm_event_t pomodoro_tick(pomodoro_t *p, uint32_t now_ms);
uint32_t   pomodoro_remaining_ms(const pomodoro_t *p, uint32_t now_ms);
#endif
```
Behavior contract (drives the tests):
- `init`: state=PM_IDLE, stats zeroed, focus_count=0.
- `start_focus` (from IDLE or after break): state=PM_FOCUS, phase_end=now+focus_min*60000.
- `tick` when now≥phase_end in PM_FOCUS: stats.completed++, stats.streak++, stats.focus_seconds += focus_min*60, focus_count++, state=PM_ALARM (alarm_is_focus=true), returns PM_EV_FOCUS_ENDED.
- `acknowledge` in PM_ALARM: if focus_count % long_every == 0 → PM_BREAK_LONG else PM_BREAK_SHORT; phase_end=now+break*60000.
- `tick` when now≥phase_end in PM_BREAK_*: state=PM_IDLE, returns PM_EV_BREAK_ENDED (UI plays gentle chime; next focus requires explicit start).
- `pause`: capture paused_left_ms=remaining, resume_state=state, state=PM_PAUSED. `resume`: phase_end=now+paused_left_ms, state=resume_state.
- `skip`: from PM_FOCUS/PM_ALARM → streak=0, state=PM_IDLE (no completion credit). From PM_BREAK_* → state=PM_IDLE.
- `add5`: phase_end += 5*60000 (only in a running phase).
- `remaining_ms`: PM_PAUSED→paused_left_ms; running phase→(phase_end>now? phase_end-now:0); else 0.

- [ ] **Step 1: Write the failing tests**

`tests/test_pomodoro.c`:
```c
#include "greatest.h"
#include "pomodoro.h"

static pm_config_t CFG = { .focus_min = 25, .short_min = 5, .long_min = 15, .long_every = 4 };
#define MIN(n) ((uint32_t)(n) * 60000u)

TEST starts_idle(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    ASSERT_EQ(PM_IDLE, p.state);
    ASSERT_EQ(0, p.stats.completed);
    PASS();
}

TEST focus_runs_then_alarms(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    ASSERT_EQ(PM_FOCUS, p.state);
    ASSERT_EQ(MIN(25), pomodoro_remaining_ms(&p, 0));
    ASSERT_EQ(PM_EV_NONE, pomodoro_tick(&p, MIN(25) - 1));
    ASSERT_EQ(PM_EV_FOCUS_ENDED, pomodoro_tick(&p, MIN(25)));
    ASSERT_EQ(PM_ALARM, p.state);
    ASSERT_EQ(1, p.stats.completed);
    ASSERT_EQ(1, p.stats.streak);
    PASS();
}

TEST ack_goes_to_short_break(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    pomodoro_tick(&p, MIN(25));
    pomodoro_acknowledge(&p, MIN(25));
    ASSERT_EQ(PM_BREAK_SHORT, p.state);
    ASSERT_EQ(MIN(5), pomodoro_remaining_ms(&p, MIN(25)));
    PASS();
}

TEST fourth_focus_earns_long_break(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    uint32_t t = 0;
    for (int i = 0; i < 3; i++) {
        pomodoro_start_focus(&p, t); t += MIN(25);
        pomodoro_tick(&p, t); pomodoro_acknowledge(&p, t);
        t += MIN(5); pomodoro_tick(&p, t);   // break ends -> idle
    }
    pomodoro_start_focus(&p, t); t += MIN(25);
    pomodoro_tick(&p, t); pomodoro_acknowledge(&p, t);
    ASSERT_EQ(PM_BREAK_LONG, p.state);
    ASSERT_EQ(4, p.stats.completed);
    PASS();
}

TEST break_end_returns_to_idle(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    pomodoro_tick(&p, MIN(25)); pomodoro_acknowledge(&p, MIN(25));
    ASSERT_EQ(PM_EV_BREAK_ENDED, pomodoro_tick(&p, MIN(30)));
    ASSERT_EQ(PM_IDLE, p.state);
    PASS();
}

TEST pause_resume_preserves_remaining(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    pomodoro_pause(&p, MIN(10));
    ASSERT_EQ(PM_PAUSED, p.state);
    ASSERT_EQ(MIN(15), pomodoro_remaining_ms(&p, MIN(999)));
    pomodoro_resume(&p, MIN(100));
    ASSERT_EQ(PM_FOCUS, p.state);
    ASSERT_EQ(MIN(15), pomodoro_remaining_ms(&p, MIN(100)));
    PASS();
}

TEST add5_extends_phase(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    pomodoro_add5(&p, 0);
    ASSERT_EQ(MIN(30), pomodoro_remaining_ms(&p, 0));
    PASS();
}

TEST skip_focus_breaks_streak_and_idles(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    pomodoro_tick(&p, MIN(25)); pomodoro_acknowledge(&p, MIN(25));  // streak=1
    pomodoro_tick(&p, MIN(30));                                    // idle
    pomodoro_start_focus(&p, MIN(30));
    pomodoro_skip(&p, MIN(31));
    ASSERT_EQ(PM_IDLE, p.state);
    ASSERT_EQ(0, p.stats.streak);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(starts_idle);
    RUN_TEST(focus_runs_then_alarms);
    RUN_TEST(ack_goes_to_short_break);
    RUN_TEST(fourth_focus_earns_long_break);
    RUN_TEST(break_end_returns_to_idle);
    RUN_TEST(pause_resume_preserves_remaining);
    RUN_TEST(add5_extends_phase);
    RUN_TEST(skip_focus_breaks_streak_and_idles);
    GREATEST_MAIN_END();
}
```
Add to `tests/CMakeLists.txt`: `add_core_test(test_pomodoro ${CORE}/pomodoro.c)`

- [ ] **Step 2: Run — verify it fails to build (no pomodoro.c yet)**

Run: `powershell -File tools/test.ps1`
Expected: configure/build FAILS — `pomodoro.h: No such file` or undefined references.

- [ ] **Step 3: Write pomodoro.h and pomodoro.c**

Create `src/core/pomodoro.h` exactly as in the Interfaces block above. Create `src/core/pomodoro.c`:
```c
#include "pomodoro.h"

static uint32_t phase_len_ms(const pomodoro_t *p) {
    switch (p->state) {
        case PM_FOCUS:       return (uint32_t)p->cfg.focus_min * 60000u;
        case PM_BREAK_SHORT: return (uint32_t)p->cfg.short_min * 60000u;
        case PM_BREAK_LONG:  return (uint32_t)p->cfg.long_min  * 60000u;
        default:             return 0;
    }
}

void pomodoro_init(pomodoro_t *p, pm_config_t cfg) {
    pomodoro_t z = {0};
    *p = z;
    p->cfg = cfg;
    p->state = PM_IDLE;
}

void pomodoro_start_focus(pomodoro_t *p, uint32_t now_ms) {
    p->state = PM_FOCUS;
    p->phase_end_ms = now_ms + (uint32_t)p->cfg.focus_min * 60000u;
}

void pomodoro_pause(pomodoro_t *p, uint32_t now_ms) {
    if (p->state != PM_FOCUS && p->state != PM_BREAK_SHORT && p->state != PM_BREAK_LONG) return;
    p->paused_left_ms = pomodoro_remaining_ms(p, now_ms);
    p->resume_state = p->state;
    p->state = PM_PAUSED;
}

void pomodoro_resume(pomodoro_t *p, uint32_t now_ms) {
    if (p->state != PM_PAUSED) return;
    p->state = p->resume_state;
    p->phase_end_ms = now_ms + p->paused_left_ms;
}

void pomodoro_skip(pomodoro_t *p, uint32_t now_ms) {
    (void)now_ms;
    if (p->state == PM_FOCUS || p->state == PM_ALARM) p->stats.streak = 0;
    p->state = PM_IDLE;
}

void pomodoro_add5(pomodoro_t *p, uint32_t now_ms) {
    (void)now_ms;
    if (p->state == PM_FOCUS || p->state == PM_BREAK_SHORT || p->state == PM_BREAK_LONG)
        p->phase_end_ms += 5u * 60000u;
}

void pomodoro_acknowledge(pomodoro_t *p, uint32_t now_ms) {
    if (p->state != PM_ALARM) return;
    if (p->cfg.long_every != 0 && (p->focus_count % p->cfg.long_every) == 0) {
        p->state = PM_BREAK_LONG;
        p->phase_end_ms = now_ms + (uint32_t)p->cfg.long_min * 60000u;
    } else {
        p->state = PM_BREAK_SHORT;
        p->phase_end_ms = now_ms + (uint32_t)p->cfg.short_min * 60000u;
    }
}

pm_event_t pomodoro_tick(pomodoro_t *p, uint32_t now_ms) {
    if (p->state == PM_FOCUS && now_ms >= p->phase_end_ms) {
        p->stats.completed++;
        p->stats.streak++;
        p->stats.focus_seconds += (uint32_t)p->cfg.focus_min * 60u;
        p->focus_count++;
        p->alarm_is_focus = true;
        p->state = PM_ALARM;
        return PM_EV_FOCUS_ENDED;
    }
    if ((p->state == PM_BREAK_SHORT || p->state == PM_BREAK_LONG) && now_ms >= p->phase_end_ms) {
        p->state = PM_IDLE;
        return PM_EV_BREAK_ENDED;
    }
    return PM_EV_NONE;
}

uint32_t pomodoro_remaining_ms(const pomodoro_t *p, uint32_t now_ms) {
    if (p->state == PM_PAUSED) return p->paused_left_ms;
    if (p->state == PM_FOCUS || p->state == PM_BREAK_SHORT || p->state == PM_BREAK_LONG) {
        (void)phase_len_ms;
        return p->phase_end_ms > now_ms ? p->phase_end_ms - now_ms : 0;
    }
    return 0;
}
```

- [ ] **Step 4: Run — verify green**

Run: `powershell -File tools/test.ps1`
Expected: `test_pomodoro` passes all 8 cases; ctest `100% tests passed`.

- [ ] **Step 5: Commit**

```bash
git add src/core/pomodoro.h src/core/pomodoro.c tests/test_pomodoro.c tests/CMakeLists.txt
git commit -m "feat(core): pomodoro session state machine with tests"
```

---

### Task 4: `core/beacon` — packet codec + CRC16 + OOK Manchester (TDD)

**Files:**
- Create: `src/core/beacon.h`, `src/core/beacon.c`, `tests/test_beacon.c`
- Modify: `tests/CMakeLists.txt` (`add_core_test(test_beacon ${CORE}/beacon.c)`)

**Interfaces:**
- Produces (verbatim header):
```c
// src/core/beacon.h
#ifndef WILIDORO_BEACON_H
#define WILIDORO_BEACON_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define BEACON_MAGIC0 0x57   /* 'W' */
#define BEACON_MAGIC1 0x44   /* 'D' */
#define BEACON_VERSION 1
#define BEACON_NAME_LEN 8
#define BEACON_WIRE_LEN 16   /* magic2 ver1 name8 state1 min1 done1 crc16 */
#define BEACON_HALFBIT_US 500u
#define BEACON_PREAMBLE_BITS 8
#define BEACON_MAX_DURS (BEACON_PREAMBLE_BITS*2 + BEACON_WIRE_LEN*8*2 + 4)

typedef enum { BST_IDLE = 0, BST_FOCUS = 1, BST_BREAK = 2 } beacon_state_t;
typedef struct {
    char           name[BEACON_NAME_LEN];  /* space-padded ASCII, not NUL-terminated */
    beacon_state_t state;
    uint8_t        minutes_left;
    uint8_t        completed;
} beacon_msg_t;

uint16_t beacon_crc16(const uint8_t *data, size_t len);          /* CRC-16/CCITT-FALSE, init 0xFFFF, poly 0x1021 */
void     beacon_pack(const beacon_msg_t *m, uint8_t out[BEACON_WIRE_LEN]);
bool     beacon_unpack(const uint8_t in[BEACON_WIRE_LEN], beacon_msg_t *out);  /* false on magic/ver/crc mismatch */
size_t   beacon_ook_encode(const uint8_t payload[BEACON_WIRE_LEN], uint32_t *durs, size_t max, bool *start_level);
bool     beacon_ook_decode(const uint32_t *durs, size_t n, uint8_t payload[BEACON_WIRE_LEN]);
#endif
```
Encoding: preamble = `BEACON_PREAMBLE_BITS` of `1` (Manchester), then each payload bit MSB-first as Manchester (bit 1 = high-then-low, bit 0 = low-then-high), each half-bit = `BEACON_HALFBIT_US`. `beacon_ook_encode` emits a run-length duration list (consecutive same-level half-bits merged) with `*start_level` = level of the first duration. `beacon_ook_decode` quantizes durations to half-bit units, reconstructs the half-bit level stream, syncs on the preamble, and Manchester-decodes 16 bytes; returns false if it cannot sync or lengths are implausible.

- [ ] **Step 1: Write the failing tests**

`tests/test_beacon.c`:
```c
#include "greatest.h"
#include "beacon.h"
#include <string.h>

static beacon_msg_t SAMPLE(void) {
    beacon_msg_t m; memcpy(m.name, "DAVE    ", 8);
    m.state = BST_FOCUS; m.minutes_left = 18; m.completed = 2;
    return m;
}

TEST crc_detects_single_bit_flip(void) {
    uint8_t buf[8] = {1,2,3,4,5,6,7,8};
    uint16_t a = beacon_crc16(buf, 8);
    buf[3] ^= 0x01;
    ASSERT(a != beacon_crc16(buf, 8));
    PASS();
}

TEST pack_unpack_roundtrip(void) {
    beacon_msg_t m = SAMPLE(), out;
    uint8_t wire[BEACON_WIRE_LEN];
    beacon_pack(&m, wire);
    ASSERT(beacon_unpack(wire, &out));
    ASSERT_EQ(0, memcmp(m.name, out.name, 8));
    ASSERT_EQ(BST_FOCUS, out.state);
    ASSERT_EQ(18, out.minutes_left);
    ASSERT_EQ(2, out.completed);
    PASS();
}

TEST unpack_rejects_bad_magic(void) {
    beacon_msg_t m = SAMPLE(), out;
    uint8_t wire[BEACON_WIRE_LEN];
    beacon_pack(&m, wire);
    wire[0] ^= 0xFF;
    ASSERT_FALSE(beacon_unpack(wire, &out));
    PASS();
}

TEST unpack_rejects_corrupt_crc(void) {
    beacon_msg_t m = SAMPLE(), out;
    uint8_t wire[BEACON_WIRE_LEN];
    beacon_pack(&m, wire);
    wire[9] ^= 0x20;   /* flip a payload bit; CRC must fail */
    ASSERT_FALSE(beacon_unpack(wire, &out));
    PASS();
}

TEST ook_roundtrip(void) {
    beacon_msg_t m = SAMPLE();
    uint8_t wire[BEACON_WIRE_LEN], back[BEACON_WIRE_LEN];
    beacon_pack(&m, wire);
    uint32_t durs[BEACON_MAX_DURS];
    bool start_level;
    size_t n = beacon_ook_encode(wire, durs, BEACON_MAX_DURS, &start_level);
    ASSERT(n > 0);
    ASSERT(beacon_ook_decode(durs, n, back));
    ASSERT_EQ(0, memcmp(wire, back, BEACON_WIRE_LEN));
    PASS();
}

TEST ook_decode_rejects_noise(void) {
    uint32_t noise[16] = { 137, 900, 42, 3100, 12, 77, 2200, 5, 640, 51, 990, 8, 33, 1200, 7, 410 };
    uint8_t back[BEACON_WIRE_LEN];
    ASSERT_FALSE(beacon_ook_decode(noise, 16, back));
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(crc_detects_single_bit_flip);
    RUN_TEST(pack_unpack_roundtrip);
    RUN_TEST(unpack_rejects_bad_magic);
    RUN_TEST(unpack_rejects_corrupt_crc);
    RUN_TEST(ook_roundtrip);
    RUN_TEST(ook_decode_rejects_noise);
    GREATEST_MAIN_END();
}
```
Add `add_core_test(test_beacon ${CORE}/beacon.c)` to `tests/CMakeLists.txt`.

- [ ] **Step 2: Run — verify build failure**

Run: `powershell -File tools/test.ps1`
Expected: FAILS to build (`beacon.h` not found).

- [ ] **Step 3: Write beacon.c**

Create `src/core/beacon.c`:
```c
#include "beacon.h"
#include <string.h>

uint16_t beacon_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

void beacon_pack(const beacon_msg_t *m, uint8_t out[BEACON_WIRE_LEN]) {
    out[0] = BEACON_MAGIC0;
    out[1] = BEACON_MAGIC1;
    out[2] = BEACON_VERSION;
    memcpy(&out[3], m->name, BEACON_NAME_LEN);   /* bytes 3..10 */
    out[11] = (uint8_t)m->state;
    out[12] = m->minutes_left;
    out[13] = m->completed;
    uint16_t crc = beacon_crc16(out, 14);
    out[14] = (uint8_t)(crc >> 8);
    out[15] = (uint8_t)(crc & 0xFF);
}

bool beacon_unpack(const uint8_t in[BEACON_WIRE_LEN], beacon_msg_t *out) {
    if (in[0] != BEACON_MAGIC0 || in[1] != BEACON_MAGIC1) return false;
    if (in[2] != BEACON_VERSION) return false;
    uint16_t crc = beacon_crc16(in, 14);
    if (in[14] != (uint8_t)(crc >> 8) || in[15] != (uint8_t)(crc & 0xFF)) return false;
    memcpy(out->name, &in[3], BEACON_NAME_LEN);
    out->state = (beacon_state_t)in[11];
    out->minutes_left = in[12];
    out->completed = in[13];
    return true;
}

/* --- OOK Manchester physical layer --- */
/* Build a half-bit level stream: preamble (Manchester 1s) then payload bits MSB-first.
   Manchester: bit 1 -> {high, low}; bit 0 -> {low, high}. */
static size_t build_halfbits(const uint8_t payload[BEACON_WIRE_LEN], uint8_t *hb) {
    size_t k = 0;
    for (int i = 0; i < BEACON_PREAMBLE_BITS; i++) { hb[k++] = 1; hb[k++] = 0; }
    for (int i = 0; i < BEACON_WIRE_LEN; i++) {
        for (int b = 7; b >= 0; b--) {
            uint8_t bit = (payload[i] >> b) & 1u;
            if (bit) { hb[k++] = 1; hb[k++] = 0; }
            else     { hb[k++] = 0; hb[k++] = 1; }
        }
    }
    return k;
}

size_t beacon_ook_encode(const uint8_t payload[BEACON_WIRE_LEN], uint32_t *durs, size_t max, bool *start_level) {
    uint8_t hb[(BEACON_PREAMBLE_BITS + BEACON_WIRE_LEN * 8) * 2];
    size_t nhb = build_halfbits(payload, hb);
    if (nhb == 0) return 0;
    *start_level = hb[0];
    size_t n = 0;
    uint32_t run = 0; uint8_t cur = hb[0];
    for (size_t i = 0; i < nhb; i++) {
        if (hb[i] == cur) { run++; }
        else {
            if (n >= max) return 0;
            durs[n++] = run * BEACON_HALFBIT_US;
            cur = hb[i]; run = 1;
        }
    }
    if (n >= max) return 0;
    durs[n++] = run * BEACON_HALFBIT_US;
    return n;
}

bool beacon_ook_decode(const uint32_t *durs, size_t n, uint8_t payload[BEACON_WIRE_LEN]) {
    /* Expand run-length durations back into a half-bit level stream (start level unknown:
       assume first run is level 1, i.e. preamble starts high). */
    uint8_t hb[(BEACON_PREAMBLE_BITS + BEACON_WIRE_LEN * 8) * 2 + 8];
    size_t nhb = 0; uint8_t level = 1;
    const uint32_t q = BEACON_HALFBIT_US;
    for (size_t i = 0; i < n; i++) {
        uint32_t units = (durs[i] + q / 2) / q;           /* nearest half-bit count */
        if (units == 0 || units > 4) return false;         /* implausible pulse */
        for (uint32_t u = 0; u < units && nhb < sizeof(hb); u++) hb[nhb++] = level;
        level ^= 1u;
    }
    size_t need = (BEACON_PREAMBLE_BITS + BEACON_WIRE_LEN * 8) * 2;
    if (nhb < need) return false;
    /* verify preamble */
    for (int i = 0; i < BEACON_PREAMBLE_BITS; i++)
        if (hb[i*2] != 1 || hb[i*2+1] != 0) return false;
    /* Manchester-decode payload */
    size_t off = BEACON_PREAMBLE_BITS * 2;
    for (int i = 0; i < BEACON_WIRE_LEN; i++) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++) {
            uint8_t h0 = hb[off++], h1 = hb[off++];
            if (h0 == 1 && h1 == 0)      byte = (uint8_t)((byte << 1) | 1u);
            else if (h0 == 0 && h1 == 1) byte = (uint8_t)(byte << 1);
            else return false;
        }
        payload[i] = byte;
    }
    return true;
}
```

- [ ] **Step 4: Run — verify green**

Run: `powershell -File tools/test.ps1`
Expected: `test_beacon` passes all 6 cases.

- [ ] **Step 5: Commit**

```bash
git add src/core/beacon.h src/core/beacon.c tests/test_beacon.c tests/CMakeLists.txt
git commit -m "feat(core): beacon packet codec + CRC16 + OOK Manchester with tests"
```

---

### Task 5: `core/gestures` — IMU gesture classifier (TDD)

**Files:**
- Create: `src/core/gestures.h`, `src/core/gestures.c`, `tests/test_gestures.c`
- Modify: `tests/CMakeLists.txt` (`add_core_test(test_gestures ${CORE}/gestures.c)`)

**Interfaces:**
- Produces (verbatim header):
```c
// src/core/gestures.h
#ifndef WILIDORO_GESTURES_H
#define WILIDORO_GESTURES_H
#include <stdint.h>
#include <stdbool.h>

typedef enum { GEV_NONE, GEV_FLIP_DOWN, GEV_FLIP_UP, GEV_SHAKE, GEV_PICKUP } gesture_event_t;

/* Tunables (units: g). az ~ +1 face-up, ~ -1 face-down. */
#define G_FLIP_DOWN_AZ  (-0.6f)
#define G_FLIP_UP_AZ    ( 0.6f)
#define G_FLIP_HOLD_MS  400u
#define G_SHAKE_JERK    (2.5f)   /* summed |delta accel| over window to trigger */
#define G_SHAKE_WIN_MS  300u
#define G_PICKUP_DEV    (0.35f)  /* |magnitude-1g| after stillness */
#define G_STILL_DEV     (0.08f)
#define G_STILL_MS      800u

typedef struct {
    int      face;            /* +1 up, -1 down, 0 unknown */
    uint32_t face_since_ms;
    float    px, py, pz;      /* previous sample */
    bool     have_prev;
    float    jerk;            /* decaying jerk accumulator */
    uint32_t jerk_ms;
    uint32_t still_since_ms;  /* when device became still (or 0) */
    bool     was_still;
} gesture_state_t;

void            gesture_init(gesture_state_t *g);
gesture_event_t gesture_feed(gesture_state_t *g, float ax, float ay, float az, uint32_t now_ms);
#endif
```
Contract: `gesture_feed` returns at most one event per call. FLIP_DOWN fires when az stays below `G_FLIP_DOWN_AZ` for `G_FLIP_HOLD_MS`; FLIP_UP symmetric above `G_FLIP_UP_AZ`; each face reported once per crossing. SHAKE fires when summed inter-sample |Δaccel| within `G_SHAKE_WIN_MS` exceeds `G_SHAKE_JERK`. PICKUP fires when, after ≥`G_STILL_MS` of stillness (|‖a‖−1|<`G_STILL_DEV`), a sample deviates by >`G_PICKUP_DEV`.

- [ ] **Step 1: Write failing tests**

`tests/test_gestures.c`:
```c
#include "greatest.h"
#include "gestures.h"

/* Feed a steady vector for a span, sampling every 50ms; return the last event seen. */
static gesture_event_t feed_steady(gesture_state_t *g, float ax, float ay, float az,
                                   uint32_t *t, uint32_t span_ms) {
    gesture_event_t last = GEV_NONE;
    for (uint32_t e = *t + span_ms; *t <= e; *t += 50) {
        gesture_event_t ev = gesture_feed(g, ax, ay, az, *t);
        if (ev != GEV_NONE) last = ev;
    }
    return last;
}

TEST flip_down_after_hold(void) {
    gesture_state_t g; gesture_init(&g);
    uint32_t t = 0;
    feed_steady(&g, 0, 0, 1.0f, &t, 500);          /* settle face-up */
    gesture_event_t ev = feed_steady(&g, 0, 0, -1.0f, &t, 600);
    ASSERT_EQ(GEV_FLIP_DOWN, ev);
    PASS();
}

TEST no_flip_if_not_held(void) {
    gesture_state_t g; gesture_init(&g);
    uint32_t t = 0;
    feed_steady(&g, 0, 0, 1.0f, &t, 500);
    /* only 100ms face-down, less than G_FLIP_HOLD_MS */
    gesture_event_t ev = feed_steady(&g, 0, 0, -1.0f, &t, 100);
    ASSERT_EQ(GEV_NONE, ev);
    PASS();
}

TEST shake_triggers(void) {
    gesture_state_t g; gesture_init(&g);
    uint32_t t = 0;
    gesture_event_t last = GEV_NONE;
    for (int i = 0; i < 8; i++) {
        float s = (i & 1) ? 2.0f : -2.0f;   /* big alternating swings */
        gesture_event_t ev = gesture_feed(&g, s, 0, 0, t); t += 30;
        if (ev == GEV_SHAKE) last = ev;
    }
    ASSERT_EQ(GEV_SHAKE, last);
    PASS();
}

TEST pickup_after_stillness(void) {
    gesture_state_t g; gesture_init(&g);
    uint32_t t = 0;
    feed_steady(&g, 0, 0, 1.0f, &t, 1000);          /* still >= G_STILL_MS */
    gesture_event_t ev = gesture_feed(&g, 0.5f, 0.4f, 1.0f, t);  /* jolt */
    ASSERT_EQ(GEV_PICKUP, ev);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(flip_down_after_hold);
    RUN_TEST(no_flip_if_not_held);
    RUN_TEST(shake_triggers);
    RUN_TEST(pickup_after_stillness);
    GREATEST_MAIN_END();
}
```
Add `add_core_test(test_gestures ${CORE}/gestures.c)` to CMake.

- [ ] **Step 2: Run — verify build failure**

Run: `powershell -File tools/test.ps1`
Expected: FAILS (`gestures.h` not found).

- [ ] **Step 3: Write gestures.c**

Create `src/core/gestures.c`:
```c
#include "gestures.h"
#include <math.h>

void gesture_init(gesture_state_t *g) {
    gesture_state_t z = {0};
    *g = z;
    g->face = 0;
}

gesture_event_t gesture_feed(gesture_state_t *g, float ax, float ay, float az, uint32_t now_ms) {
    gesture_event_t out = GEV_NONE;

    /* --- jerk accumulator for shake --- */
    if (g->have_prev) {
        float dj = fabsf(ax - g->px) + fabsf(ay - g->py) + fabsf(az - g->pz);
        if (now_ms - g->jerk_ms > G_SHAKE_WIN_MS) g->jerk = 0.0f;
        g->jerk_ms = now_ms;
        g->jerk += dj;
        if (g->jerk >= G_SHAKE_JERK) { g->jerk = 0.0f; out = GEV_SHAKE; }
    }

    /* --- stillness / pickup --- */
    float mag = sqrtf(ax*ax + ay*ay + az*az);
    float dev = fabsf(mag - 1.0f);
    if (dev < G_STILL_DEV) {
        if (!g->was_still) { g->was_still = true; g->still_since_ms = now_ms; }
    } else {
        if (g->was_still && (now_ms - g->still_since_ms) >= G_STILL_MS && dev > G_PICKUP_DEV) {
            if (out == GEV_NONE) out = GEV_PICKUP;
        }
        g->was_still = false;
    }

    /* --- flip detection with hold --- */
    int face_now = g->face;
    if (az <= G_FLIP_DOWN_AZ) face_now = -1;
    else if (az >= G_FLIP_UP_AZ) face_now = 1;
    if (face_now != g->face) {
        g->face = face_now;
        g->face_since_ms = now_ms;
        g->face_reported = false;
    } else if (!g->face_reported && (now_ms - g->face_since_ms) >= G_FLIP_HOLD_MS) {
        g->face_reported = true;
        if (out == GEV_NONE) out = (g->face == -1) ? GEV_FLIP_DOWN : GEV_FLIP_UP;
    }

    g->px = ax; g->py = ay; g->pz = az; g->have_prev = true;
    return out;
}
```
Note: add `bool face_reported;` to `gesture_state_t` in the header (the `.c` uses it). Update the header struct to include it right after `uint32_t face_since_ms;`.

- [ ] **Step 4: Run — verify green**

Run: `powershell -File tools/test.ps1`
Expected: `test_gestures` passes all 4 cases.

- [ ] **Step 5: Commit**

```bash
git add src/core/gestures.h src/core/gestures.c tests/test_gestures.c tests/CMakeLists.txt
git commit -m "feat(core): IMU gesture classifier (flip/shake/pickup) with tests"
```

---

### Task 6: `core/dimming` — auto-dim curve (TDD)

**Files:**
- Create: `src/core/dimming.h`, `src/core/dimming.c`, `tests/test_dimming.c`
- Modify: `tests/CMakeLists.txt` (`add_core_test(test_dimming ${CORE}/dimming.c)`)

**Interfaces:**
- Produces (verbatim header):
```c
// src/core/dimming.h
#ifndef WILIDORO_DIMMING_H
#define WILIDORO_DIMMING_H
#include <stdint.h>

#define DIM_MIN_PCT   40.0f
#define DIM_FULL_LUX  400.0f
#define DIM_ALPHA     0.25f
#define DIM_LED_FLOOR 8      /* min LED brightness 0..255 */

typedef struct { float smooth; } dim_state_t;

void    dim_init(dim_state_t *d, float initial_pct);
uint8_t dim_apply(dim_state_t *d, float lux);        /* -> backlight 0..100, smoothed */
uint8_t dim_led_brightness(uint8_t backlight_pct);   /* -> WS2812 brightness 0..255 */
#endif
```
Curve mirrors sensorview: `target = 40 + (lux/400)*60`, clamped to [40,100]; `smooth += (target-smooth)*0.25`. LED brightness scales backlight% to [DIM_LED_FLOOR..255].

- [ ] **Step 1: Write failing tests**

`tests/test_dimming.c`:
```c
#include "greatest.h"
#include "dimming.h"

TEST floor_in_darkness(void) {
    dim_state_t d; dim_init(&d, 100.0f);
    uint8_t pct = 0;
    for (int i = 0; i < 50; i++) pct = dim_apply(&d, 0.0f);  /* converge */
    ASSERT_EQ(40, pct);
    PASS();
}

TEST full_in_bright_light(void) {
    dim_state_t d; dim_init(&d, 40.0f);
    uint8_t pct = 0;
    for (int i = 0; i < 50; i++) pct = dim_apply(&d, 1000.0f);
    ASSERT_EQ(100, pct);
    PASS();
}

TEST smoothing_is_gradual(void) {
    dim_state_t d; dim_init(&d, 100.0f);
    uint8_t first = dim_apply(&d, 0.0f);   /* one step toward 40 from 100 */
    ASSERT(first > 40 && first < 100);     /* not a jump */
    PASS();
}

TEST led_brightness_has_floor(void) {
    ASSERT_EQ(255, dim_led_brightness(100));
    ASSERT(dim_led_brightness(0) >= DIM_LED_FLOOR);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(floor_in_darkness);
    RUN_TEST(full_in_bright_light);
    RUN_TEST(smoothing_is_gradual);
    RUN_TEST(led_brightness_has_floor);
    GREATEST_MAIN_END();
}
```
Add `add_core_test(test_dimming ${CORE}/dimming.c)`.

- [ ] **Step 2: Run — verify build failure**

Run: `powershell -File tools/test.ps1`
Expected: FAILS (`dimming.h` not found).

- [ ] **Step 3: Write dimming.c**

Create `src/core/dimming.c`:
```c
#include "dimming.h"

void dim_init(dim_state_t *d, float initial_pct) { d->smooth = initial_pct; }

uint8_t dim_apply(dim_state_t *d, float lux) {
    float target = DIM_MIN_PCT + (lux / DIM_FULL_LUX) * (100.0f - DIM_MIN_PCT);
    if (target > 100.0f) target = 100.0f;
    if (target < DIM_MIN_PCT) target = DIM_MIN_PCT;
    d->smooth += (target - d->smooth) * DIM_ALPHA;
    if (d->smooth > 100.0f) d->smooth = 100.0f;
    if (d->smooth < DIM_MIN_PCT) d->smooth = DIM_MIN_PCT;
    return (uint8_t)(d->smooth + 0.5f);
}

uint8_t dim_led_brightness(uint8_t backlight_pct) {
    if (backlight_pct > 100) backlight_pct = 100;
    int b = DIM_LED_FLOOR + (255 - DIM_LED_FLOOR) * backlight_pct / 100;
    return (uint8_t)b;
}
```

- [ ] **Step 4: Run — verify green (whole suite)**

Run: `powershell -File tools/test.ps1`
Expected: all 4 test binaries pass; ctest `100% tests passed, 0 failed out of 5`.

- [ ] **Step 5: Commit**

```bash
git add src/core/dimming.h src/core/dimming.c tests/test_dimming.c tests/CMakeLists.txt
git commit -m "feat(core): auto-dim curve + LED brightness mapping with tests"
```

---

### Task 7: LVGL config + shared board header + HAL interface stub

**Files:**
- Create: `boards/freewili2.h`, `config/lv_conf.h`, `src/hal/hal.h`

**Interfaces:**
- Produces: the flash-boot SDK board header, the shared `lv_conf.h` (device + sim), and the `hal.h` interface that Plan B implements. No behavior yet — this task only has to compile in later tasks.

- [ ] **Step 1: Write the SDK board header (flash boot)**

`boards/freewili2.h`:
```c
#ifndef WILIDORO_BOARD_FREEWILI2_H
#define WILIDORO_BOARD_FREEWILI2_H
// RP2350B (48 GPIO), XIP from 16MB W25Q080-class flash.
#define PICO_RP2350A 0
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif
#ifndef PICO_BOOT_STAGE2_CHOOSE_W25Q080
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#endif
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif
#endif
```

- [ ] **Step 2: Write the shared lv_conf.h**

`config/lv_conf.h` — start from the LVGL 9.2.2 template (`third_party/lvgl/lv_conf_template.h`) and apply these deltas (only the lines that differ from the template need changing; the values below are authoritative for this project):
```c
/* excerpt of the settings that MUST hold — see third_party/lvgl/lv_conf_template.h for the rest */
#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (64 * 1024U)
#define LV_DEF_REFR_PERIOD 16
#define LV_DPI_DEF 130
#define LV_USE_OS LV_OS_NONE
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_DRAW_UNIT_CNT 1
#define LV_USE_LOG 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_48 1   /* large timer digits (Plan B) */
#define LV_FONT_DEFAULT &lv_font_montserrat_16
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_FLEX 1
#define LV_USE_GRID 1
#define LV_USE_LED 1              /* on-screen LED strip in the simulator */

/* Simulator-only display/input driver (device build leaves these off). */
#ifdef WILIDORO_SIM
#  define LV_USE_SDL 1
#  define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
#  define LV_SDL_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT
#endif
```

- [ ] **Step 3: Write the HAL interface (declarations only)**

`src/hal/hal.h`:
```c
#ifndef WILIDORO_HAL_H
#define WILIDORO_HAL_H
#include <stdint.h>
#include <stdbool.h>
#include "beacon.h"

/* Millisecond clock (device: to_ms_since_boot; sim: SDL_GetTicks). */
uint32_t hal_now_ms(void);

/* Physical buttons: bitmask of currently-pressed keys. */
typedef enum { HAL_BTN_A=1, HAL_BTN_B=2, HAL_BTN_X=4, HAL_BTN_Y=8, HAL_BTN_DPAD=16 } hal_btn_t;
uint32_t hal_buttons(void);

/* 16 LEDs. */
void hal_led_set(int i, uint8_t r, uint8_t g, uint8_t b);
void hal_led_brightness(uint8_t level);   /* 0..255 */
void hal_led_show(void);

/* Audio: enqueue a synthesized tone (freq Hz, duration ms, 0..255 amplitude). */
void hal_tone(uint16_t hz, uint16_t ms, uint8_t amp);
void hal_audio_idle(void);   /* power the codec down when nothing is playing */

/* Backlight 0..100 (device: GPIO25 PWM; sim: no-op or window tint). */
void hal_backlight(uint8_t pct);

/* Sensors. */
bool  hal_imu(float *ax, float *ay, float *az);   /* g; false if unavailable */
bool  hal_lux(float *lux);                          /* false if unavailable */

/* Radio beacon. */
void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN]);
bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]);  /* true if a valid frame was captured */

/* Feature availability flags, set at init (crossed-out icons on the timer face). */
typedef struct { bool radio, imu, light, audio; } hal_caps_t;
hal_caps_t hal_caps(void);
#endif
```

- [ ] **Step 4: Verify headers compile standalone**

Run:
```bash
"C:/msys64/mingw64/bin/gcc.exe" -fsyntax-only -Isrc/core -Isrc/hal -xc src/hal/hal.h
"C:/msys64/mingw64/bin/gcc.exe" -fsyntax-only -xc boards/freewili2.h
```
Expected: no output (both compile clean).

- [ ] **Step 5: Commit**

```bash
git add boards/freewili2.h config/lv_conf.h src/hal/hal.h
git commit -m "feat: LVGL config, flash-boot board header, HAL interface"
```

---

### Task 8: Device build — blank LVGL screen on hardware

**Files:**
- Create: `CMakeLists.txt` (top-level), `CMakePresets.json`, `src/target/lvgl_port.h`, `src/target/lvgl_port.c`, `src/target/bl_pwm.h`, `src/target/bl_pwm.c`, `src/target/main.c`
- Create: `tools/build.ps1`, `tools/flash.ps1`, `tools/rtt.ps1`

**Interfaces:**
- Consumes: wilibsp drivers (`board_init`, `st7796_*`, `ft6336_*`), LVGL, `config/lv_conf.h`, `boards/freewili2.h`.
- Produces: `lvgl_port_init()` / `lvgl_port_register_touch()` (used by Plan B), `bl_pwm_init()` / `bl_pwm_set(pct)` (auto-dim actuator, used in Plan C).

- [ ] **Step 1: Write the PWM backlight (reassigns GPIO25 after board_init)**

`src/target/bl_pwm.h`:
```c
#ifndef WILIDORO_BL_PWM_H
#define WILIDORO_BL_PWM_H
#include <stdint.h>
void bl_pwm_init(void);
void bl_pwm_set(uint8_t pct);   /* 0..100 */
#endif
```
`src/target/bl_pwm.c`:
```c
#include "bl_pwm.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#define BL_PIN 25
static uint s_slice;
void bl_pwm_init(void) {
    gpio_set_function(BL_PIN, GPIO_FUNC_PWM);
    s_slice = pwm_gpio_to_slice_num(BL_PIN);
    pwm_set_wrap(s_slice, 1000);
    pwm_set_enabled(s_slice, true);
    bl_pwm_set(100);
}
void bl_pwm_set(uint8_t pct) {
    if (pct > 100) pct = 100;
    pwm_set_gpio_level(BL_PIN, (uint16_t)(pct * 10));
}
```

- [ ] **Step 2: Write the LVGL port (flush + indev + tick), from the proven sensorview pattern**

`src/target/lvgl_port.h`:
```c
#ifndef WILIDORO_LVGL_PORT_H
#define WILIDORO_LVGL_PORT_H
void lvgl_port_init(void);
void lvgl_port_register_touch(void);
#endif
```
`src/target/lvgl_port.c`:
```c
#include "lvgl_port.h"
#include "lvgl.h"
#include "fw2.h"
#include "pico/stdlib.h"

#define DISP_HOR 480
#define DISP_VER 320
#define BUF_LINES 40
#define BUF_PX (DISP_HOR * BUF_LINES)
static lv_color_t s_buf1[BUF_PX];
static lv_color_t s_buf2[BUF_PX];

static uint32_t tick_get_cb(void) { return to_ms_since_boot(get_absolute_time()); }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    lv_draw_sw_rgb565_swap(px_map, (uint32_t)w * h);
    st7796_blit_rect((uint16_t)area->x1, (uint16_t)area->y1,
                     (uint16_t)area->x2, (uint16_t)area->y2, (const uint16_t *)px_map);
    lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    uint16_t x, y;
    if (ft6336_poll(&x, &y)) {
        data->point.x = x; data->point.y = y; data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void lvgl_port_init(void) {
    lv_init();
    lv_tick_set_cb(tick_get_cb);
    lv_display_t *disp = lv_display_create(DISP_HOR, DISP_VER);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, s_buf1, s_buf2, sizeof(s_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void lvgl_port_register_touch(void) {
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
}
```
Note: `st7796_blit_rect` is the blocking BSP blit; inclusive x1..x2/y1..y2 matches the BSP signature. (Plan C may switch to `st7796_flush_async` for tear-free updates.)

- [ ] **Step 3: Write main.c — boot + blank screen with a label**

`src/target/main.c`:
```c
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "bl_pwm.h"

int main(void) {
    board_init();                 // 250 MHz, clk_peri re-source, I2C1, ioexp
    st7796_init();
    st7796_fill_screen(0x0000);   // black, no boot garbage

    lvgl_port_init();
    ft6336_init();
    lvgl_port_register_touch();

    ws2812_init(pio1, 0, PIN_LED_DATA);
    ws2812_set_brightness(40);
    ws2812_clear();
    ws2812_show();

    // blank screen with a centered label to prove LVGL renders
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0C0C12), LV_PART_MAIN);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "wilidoro");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFF5B45), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(label);

    lv_timer_handler();           // render first frame
    bl_pwm_init();                // backlight PWM on AFTER first frame (reassigns GPIO25)
    DIAG("wilidoro up: sys=%u kHz\n", BOARD_SYS_CLOCK_KHZ);

    absolute_time_t next_led = get_absolute_time();
    for (;;) {
        lv_timer_handler();
        if (absolute_time_diff_us(get_absolute_time(), next_led) <= 0) {
            ws2812_show();        // re-latch (BSP first-show quirk)
            next_led = make_timeout_time_ms(250);
        }
        sleep_ms(5);
    }
}
```

- [ ] **Step 4: Write CMakeLists.txt (top-level device build)**

`CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20)
set(PICO_BOARD freewili2 CACHE STRING "Board type")
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/boards")
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/wilibsp/bsp/boards")
include(pico_sdk_import.cmake)
project(wilidoro C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

# LVGL
set(LV_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/config/lv_conf.h CACHE STRING "" FORCE)
add_subdirectory(third_party/lvgl)

# wilibsp BSP static library
add_subdirectory(wilibsp/bsp)

add_executable(wilidoro
    src/target/main.c
    src/target/lvgl_port.c
    src/target/bl_pwm.c
    src/core/pomodoro.c
    src/core/beacon.c
    src/core/gestures.c
    src/core/dimming.c
)
target_include_directories(wilidoro PRIVATE
    src/core src/hal src/target
    wilibsp/bsp
    config
)
target_link_libraries(wilidoro PRIVATE
    pico_stdlib hardware_clocks hardware_gpio hardware_spi hardware_dma
    hardware_irq hardware_pwm hardware_i2c hardware_pio
    freewili2_bsp lvgl
)
pico_enable_stdio_usb(wilidoro 0)
pico_enable_stdio_uart(wilidoro 0)
pico_set_binary_type(wilidoro default)      # XIP from flash (LVGL too big for copy_to_ram)
pico_add_extra_outputs(wilidoro)
```
`CMakePresets.json`:
```json
{
  "version": 3,
  "configurePresets": [
    { "name": "target", "generator": "Ninja", "binaryDir": "${sourceDir}/build",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "RelWithDebInfo" } }
  ],
  "buildPresets": [ { "name": "target", "configurePreset": "target" } ]
}
```

- [ ] **Step 5: Write build/flash/rtt scripts (Pico VS Code toolchain paths)**

`tools/build.ps1`:
```powershell
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$sdk  = "$env:USERPROFILE/.pico-sdk"
$SdkPath   = (Get-ChildItem "$sdk/sdk" | Sort-Object Name -Descending | Select-Object -First 1).FullName
$Toolchain = (Get-ChildItem "$sdk/toolchain" | Sort-Object Name -Descending | Select-Object -First 1).FullName
$PicotoolDir = (Get-ChildItem "$sdk/picotool" -Recurse -Filter "picotool" -Directory | Select-Object -First 1).FullName
if ($args -contains "-Clean") { Remove-Item "$root/build" -Recurse -Force -ErrorAction SilentlyContinue }
cmake -G Ninja -B "$root/build" -S "$root" -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DPICO_SDK_PATH="$SdkPath" -DPICO_TOOLCHAIN_PATH="$Toolchain" `
  -DPICO_PLATFORM=rp2350 -Dpicotool_DIR="$PicotoolDir"
cmake --build "$root/build"
```
`tools/flash.ps1`:
```powershell
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$sdk  = "$env:USERPROFILE/.pico-sdk"
$OpenOcd = (Get-ChildItem "$sdk/openocd" -Recurse -Filter "openocd.exe" | Sort-Object FullName -Descending | Select-Object -First 1).FullName
$Scripts = Join-Path (Split-Path (Split-Path $OpenOcd -Parent) -Parent) "scripts"
Get-Process openocd -ErrorAction SilentlyContinue | Stop-Process -Force
& $OpenOcd -s $Scripts -f "interface/cmsis-dap.cfg" -c "adapter speed 5000" `
  -f "target/rp2350.cfg" -c "program {$root/build/wilidoro.elf} verify reset exit"
```
`tools/rtt.ps1`:
```powershell
$ErrorActionPreference = "Stop"
$sdk = "$env:USERPROFILE/.pico-sdk"
$OpenOcd = (Get-ChildItem "$sdk/openocd" -Recurse -Filter "openocd.exe" | Sort-Object FullName -Descending | Select-Object -First 1).FullName
$Scripts = Join-Path (Split-Path (Split-Path $OpenOcd -Parent) -Parent) "scripts"
& $OpenOcd -s $Scripts -f "interface/cmsis-dap.cfg" -c "adapter speed 5000" -f "target/rp2350.cfg" `
  -c "init" -c "rtt setup 0x20000000 0x82000 `"SEGGER RTT`"" -c "rtt start" -c "rtt server start 9090 0" &
Start-Sleep 1; $c = New-Object System.Net.Sockets.TcpClient("localhost",9090)
$s = $c.GetStream(); $b = New-Object byte[] 4096
while ($true) { $n = $s.Read($b,0,4096); if ($n -gt 0) { [Console]::Write([Text.Encoding]::ASCII.GetString($b,0,$n)) } }
```

- [ ] **Step 6: Build the firmware**

Run: `powershell -File tools/build.ps1 -Clean`
Expected: compiles and links; `build/wilidoro.elf` and `build/wilidoro.uf2` produced; no SRAM-overflow linker error (proves `default` binary type is correct).

- [ ] **Step 7: Flash and verify on hardware**

Run: `powershell -File tools/flash.ps1`
Then run: `powershell -File tools/rtt.ps1` (Ctrl+C after a few seconds)
Expected (ON-HARDWARE ACCEPTANCE): the 480×320 panel shows a dark screen with a centered orange "wilidoro" label; backlight is on; RTT prints `wilidoro up: sys=250000 kHz`. (If the panel is dark/garbled, the fault is almost always the clk_peri re-source in `board_init` or the RGB565 byte-swap — check those first.)

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt CMakePresets.json src/target tools/build.ps1 tools/flash.ps1 tools/rtt.ps1
git commit -m "feat(target): device build with blank LVGL screen on hardware"
```

---

### Task 9: Simulator build — blank LVGL screen in a Windows window

**Files:**
- Create: `src/sim/main.c`, `src/sim/CMakeLists.txt`
- Create: `tools/sim.ps1`

**Interfaces:**
- Consumes: LVGL (with `LV_USE_SDL`), `config/lv_conf.h`, `core/`.
- Produces: a runnable `wilidoro_sim.exe` showing the same blank screen; the SDL HAL that Plan B's UI runs against.

- [ ] **Step 1: Write the simulator entry**

`src/sim/main.c`:
```c
#define WILIDORO_SIM 1
#include "lvgl.h"
#include <SDL2/SDL.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    lv_init();
    lv_display_t *disp = lv_sdl_window_create(480, 320);
    lv_indev_t *mouse = lv_sdl_mouse_create();
    (void)mouse; (void)disp;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0C0C12), LV_PART_MAIN);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "wilidoro (sim)");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFF5B45), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(label);

    while (1) {
        lv_timer_handler();
        SDL_Delay(5);
    }
    return 0;
}
```

- [ ] **Step 2: Write the simulator CMake (host, SDL2)**

`src/sim/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20)
project(wilidoro_sim C)
set(CMAKE_C_STANDARD 11)

add_compile_definitions(WILIDORO_SIM=1)
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../config/lv_conf.h CACHE STRING "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/lvgl lvgl_build)

find_package(SDL2 REQUIRED)

add_executable(wilidoro_sim
    main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/pomodoro.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/beacon.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/gestures.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/dimming.c
)
target_include_directories(wilidoro_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core
    ${CMAKE_CURRENT_SOURCE_DIR}/../hal
    ${CMAKE_CURRENT_SOURCE_DIR}/../../config
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(wilidoro_sim PRIVATE lvgl ${SDL2_LIBRARIES} m)
```

- [ ] **Step 3: Write tools/sim.ps1**

`tools/sim.ps1`:
```powershell
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$gcc  = "C:/msys64/mingw64/bin/gcc.exe"
$gxx  = "C:/msys64/mingw64/bin/g++.exe"
cmake -G Ninja -B "$root/build-sim" -S "$root/src/sim" `
  -DCMAKE_C_COMPILER="$gcc" -DCMAKE_CXX_COMPILER="$gxx" `
  -DCMAKE_PREFIX_PATH="C:/msys64/mingw64"
cmake --build "$root/build-sim"
& "$root/build-sim/wilidoro_sim.exe"
```
Prereq note (document in a comment at the top of the script): SDL2 must be installed in MSYS2: `pacman -S mingw-w64-x86_64-SDL2`.

- [ ] **Step 4: Build and run the simulator**

Run: `powershell -File tools/sim.ps1`
Expected (ACCEPTANCE): a 480×320 SDL window opens showing the dark background with a centered orange "wilidoro (sim)" label. Close the window / Ctrl+C to exit.

- [ ] **Step 5: Commit**

```bash
git add src/sim/main.c src/sim/CMakeLists.txt tools/sim.ps1
git commit -m "feat(sim): LVGL SDL simulator with blank screen"
```

---

## Self-Review

**Spec coverage (Plan A scope only):** The spec's foundation and pure-logic requirements are all covered — pomodoro FSM incl. alarm/acknowledge and long-break cadence (Task 3), session-only stats (Task 3, in `pm_stats_t`), beacon 16-byte packet format + CRC (Task 4), OOK physical layer implied by "wilibsp radio engine" (Task 4, resolves the no-packet-engine finding), gesture table (Task 5), auto-dim curve (Task 6), the wilibsp+LVGL architecture and dual build targets incl. simulator (Tasks 1,7,8,9). Deferred to Plan B/C by design: timer face UI + themes, Settings/Nearby screens, HAL implementations, and live hardware wiring (LED patterns, audio synthesis, IMU polling, OPT4001 auto-dim actuation, CC1101 TX/RX).

**Placeholder scan:** No TBD/TODO. Every code step contains complete, compilable code. The lv_conf.h step is an authoritative delta list against the LVGL template (the template is the vendored baseline), not a placeholder.

**Type consistency:** `hal.h` (Task 7) references `beacon.h` types (`BEACON_WIRE_LEN`) defined in Task 4 — consistent. `lvgl_port` (Task 8) uses `st7796_blit_rect` with inclusive coords per the BSP signature. `gesture_state_t` gains `bool face_reported;` — Task 5 Step 3 explicitly instructs adding it to the header struct (fix applied inline in the note).

## Roadmap — what comes after Plan A

- **Plan B — Timer UI & themes:** implement `hal.h` for both targets; build the timer-face screen driven by `core/pomodoro`; the three switchable themes (Neon Arc / Tomato Arcade / Flip Clock) as LVGL style sets; Settings and Nearby screens; softkey navigation. Deliverable: a fully usable timer in the simulator and on-device (radio/LEDs still simulated or basic).
- **Plan C — Hardware features:** LED progress bar + per-theme celebrations; on-device synthesized audio (start chimes, end alarms, focus tick, touch blips); live BMI323 gestures wired to `core/gestures`; OPT4001 → `dim_apply` → `bl_pwm` auto-dim; CC1101 beacon TX every ~10 s and RX populating the Nearby neighbor table; per-peripheral init-failure handling with crossed-out icons. Deliverable: all spec hardware features live, verified with a two-device beacon exchange.
