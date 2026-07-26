# Wilidoro Plan C3 — Tilt to pause: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the board lying face-up and level the *running* orientation — lift or tilt it and a focus session pauses; set it back down and it resumes — off by default, behind a Settings toggle.

**Architecture:** A new pure `src/core/tilt.c` turns accelerometer samples into two edge events (`TILT_EV_FLAT` / `TILT_EV_LIFTED`) using a hysteresis band, a 600 ms hold, and normalization by `‖a‖` to reject motion. It **replaces** `src/core/gestures.c`, which has had no consumer since Plan A. `hal_imu()` becomes real on both HALs (BMI323 on device, keyboard-driven fake tilt in the simulator). A single new 100 ms `sensor_cb` LVGL timer becomes the **sole owner of I2C1 sensor reads** — the IMU every call, lux every fifth — taking the lux poll out of `tick_cb`. The app acts only on transitions, and only on focus.

**Tech Stack:** C11, LVGL 9, the `wilibsp` BSP (`bmi323_init` / `bmi323_read` over I2C1 @ 0x68, already compiled into the BSP library), greatest.h for host tests, CMake + Ninja, SDL2 for the simulator.

**Spec:** `docs/superpowers/specs/2026-07-25-wilidoro-C3-tilt-pause-design.md`

## Global Constraints

- **The HAL is the only hardware seam.** `src/app/`, `src/ui/`, `src/core/` must never include a `wilibsp` or SDL header. Only `src/hal/hal_target.c` and `src/hal/hal_sim.c` may.
- **Pure logic is host-tested.** `src/core/` and `src/app/` modules stay LVGL- and hardware-free so CTest can cover them.
- **The host suite must stay at 10 binaries and pass 10/10.** Run: `powershell -File tools/test.ps1`
- **The device build must be zero-warnings with no RAM overflow.** Run: `powershell -File tools/build.ps1 -Clean` (~10 min).
- **Simulator build:** `cmake --build build-sim` (configure once with `cmake -G Ninja -B build-sim -S src/sim -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe -DCMAKE_PREFIX_PATH=C:/msys64/mingw64`).
- **Host tests compile with `-Wall -Wextra`.** No unused-parameter or sign-compare warnings.
- **All time comparisons must be wrap-safe:** `(int32_t)(now - deadline) >= 0`, never `now >= deadline`.
- **NEVER touch hardware without asking the user first** — flashing, OpenOCD/RTT, the debug probe, the camera, the microphone. Builds, host tests and the simulator are free.
- **Commit trailer on every commit:**
  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  ```

## File Structure

| File | Responsibility |
|---|---|
| `src/core/tilt.h` (create) | Event enum, four tunables, `tilt_state_t`, two function decls |
| `src/core/tilt.c` (create) | The orientation gate — the only new logic in this plan |
| `tests/test_tilt.c` (create) | 8 host tests for the gate |
| `src/core/gestures.h`, `gestures.c`, `tests/test_gestures.c` (delete) | Superseded; no consumer since Plan A |
| `tests/CMakeLists.txt`, `CMakeLists.txt`, `src/sim/CMakeLists.txt` (modify) | Swap `gestures.c` → `tilt.c` |
| `src/hal/hal_target.c` (modify) | `bmi323_init` in `hal_init`, real `hal_imu`, `caps.imu` |
| `src/hal/hal_sim.c` (modify) | `-` / `=` drive a fake tilt angle; `hal_imu` emits `sin/cos` |
| `src/app/app_model.h`, `app_model.c` (modify) | `bool tilt_pause`, defaulting to false |
| `tests/test_app_model.c` (modify) | Assert the new default |
| `src/app/app.h`, `app.c` (modify) | `app_tilt_apply()`, `sensor_cb`, the lux poll moves in, pause/resume |
| `src/ui/screen_settings.c` (modify) | "Tilt to pause" row, `"no imu"` value, Default-key re-prime |
| `docs/hardware-notes.md` (modify) | Tunables + the on-device checklist |

---

### Task 1: `core/tilt` — the pure orientation gate

Replaces the five-gesture classifier with the one rule the product actually has. Everything in this task is host-testable; no hardware, no LVGL.

**Files:**
- Create: `src/core/tilt.h`, `src/core/tilt.c`, `tests/test_tilt.c`
- Delete: `src/core/gestures.h`, `src/core/gestures.c`, `tests/test_gestures.c`
- Modify: `tests/CMakeLists.txt:19`, `CMakeLists.txt:31`, `src/sim/CMakeLists.txt:27`

**Interfaces:**
- Consumes: nothing (first task).
- Produces: `tilt_event_t` (`TILT_EV_NONE`, `TILT_EV_FLAT`, `TILT_EV_LIFTED`); `tilt_state_t`; `void tilt_init(tilt_state_t *t)`; `tilt_event_t tilt_feed(tilt_state_t *t, float ax, float ay, float az, uint32_t now_ms)`. Task 4 calls exactly these.

- [ ] **Step 1: Write the header**

Create `src/core/tilt.h`:

```c
// src/core/tilt.h -- pure orientation gate: is the board lying face-up and level?
#ifndef WILIDORO_TILT_H
#define WILIDORO_TILT_H
#include <stdint.h>
#include <stdbool.h>

/* Edge events. The caller acts on transitions only, never on the standing zone,
   which is what keeps a manual Pause taken while the board is flat from being
   instantly undone by the gate. */
typedef enum { TILT_EV_NONE, TILT_EV_FLAT, TILT_EV_LIFTED } tilt_event_t;

/* Thresholds on az/|a| -- the cosine of the tilt away from face-up level.
   Dividing by |a| is what rejects motion: a 2 g jolt can push raw az past
   TILT_FLAT_COS while the board is nowhere near level. */
#define TILT_FLAT_COS  (0.87f)   /* enter flat: within ~29.5 deg of level */
#define TILT_LIFT_COS  (0.77f)   /* leave flat: beyond ~39.7 deg */
#define TILT_HOLD_MS   (600u)    /* a new zone must persist this long to count */
#define TILT_MAG_MIN   (0.30f)   /* below this |a| the sample is discarded */

typedef struct {
    int8_t   zone;           /* committed: +1 flat, -1 lifted, 0 before priming */
    int8_t   cand;           /* pending zone being timed, 0 = nothing pending */
    uint32_t cand_since_ms;
    bool     primed;         /* the first usable sample adopts its zone silently */
} tilt_state_t;

void         tilt_init(tilt_state_t *t);
tilt_event_t tilt_feed(tilt_state_t *t, float ax, float ay, float az, uint32_t now_ms);
#endif
```

- [ ] **Step 2: Write the failing tests**

Create `tests/test_tilt.c`:

```c
#include "greatest.h"
#include "tilt.h"

/* Feed one steady vector for span_ms, sampling every 100 ms -- the same cadence
   sensor_cb uses on the device. Returns the last non-NONE event and counts how
   many fired, because "exactly once" is the property that matters most here. */
static tilt_event_t feed(tilt_state_t *s, float ax, float ay, float az,
                         uint32_t *t, uint32_t span_ms, int *count) {
    tilt_event_t last = TILT_EV_NONE;
    for (uint32_t end = *t + span_ms; *t <= end; *t += 100) {
        tilt_event_t ev = tilt_feed(s, ax, ay, az, *t);
        if (ev != TILT_EV_NONE) { last = ev; (*count)++; }
    }
    return last;
}

/* Vectors used throughout. UPRIGHT is the board stood on its edge: az ~ 0, so
   c ~ 0.05, comfortably past TILT_LIFT_COS. */
#define FLAT_X    0.0f
#define FLAT_Y    0.0f
#define FLAT_Z    1.0f
#define UP_X      0.0f
#define UP_Y      1.0f
#define UP_Z      0.05f

TEST primes_silently_then_lifts_once(void) {
    tilt_state_t s; tilt_init(&s);
    uint32_t t = 0; int n = 0;
    /* Priming adopts "flat" and must NOT report it. */
    ASSERT_EQ(TILT_EV_NONE, feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 500, &n));
    ASSERT_EQ(0, n);
    /* Stood upright: one LIFTED after the hold, and only one. */
    ASSERT_EQ(TILT_EV_LIFTED, feed(&s, UP_X, UP_Y, UP_Z, &t, 2000, &n));
    ASSERT_EQ(1, n);
    PASS();
}

TEST no_event_before_the_hold_elapses(void) {
    tilt_state_t s; tilt_init(&s);
    uint32_t t = 0; int n = 0;
    feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 500, &n);
    /* 400 ms lifted is short of TILT_HOLD_MS = 600. */
    ASSERT_EQ(TILT_EV_NONE, feed(&s, UP_X, UP_Y, UP_Z, &t, 400, &n));
    ASSERT_EQ(0, n);
    PASS();
}

TEST returning_to_flat_mid_hold_cancels(void) {
    tilt_state_t s; tilt_init(&s);
    uint32_t t = 0; int n = 0;
    feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 500, &n);
    feed(&s, UP_X, UP_Y, UP_Z, &t, 300, &n);          /* lifted, too briefly */
    feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 300, &n);    /* back flat: cancels */
    /* The hold must restart from scratch, so 400 ms still yields nothing. */
    ASSERT_EQ(TILT_EV_NONE, feed(&s, UP_X, UP_Y, UP_Z, &t, 400, &n));
    ASSERT_EQ(0, n);
    PASS();
}

TEST dead_band_holds_the_zone(void) {
    tilt_state_t s; tilt_init(&s);
    uint32_t t = 0; int n = 0;
    feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 500, &n);
    /* 35 deg: c = 0.819, inside the 0.77..0.87 band -- no zone change, ever. */
    ASSERT_EQ(TILT_EV_NONE, feed(&s, 0.0f, 0.574f, 0.819f, &t, 3000, &n));
    ASSERT_EQ(0, n);
    PASS();
}

TEST motion_is_rejected_by_normalization(void) {
    tilt_state_t s; tilt_init(&s);
    uint32_t t = 0; int n = 0;
    /* Start upright, so a spurious "flat" would be a reportable change. */
    feed(&s, UP_X, UP_Y, UP_Z, &t, 500, &n);
    /* A 2 g jolt while still upright: raw az = 1.0 clears TILT_FLAT_COS, but
       |a| = 2.24 so c = 0.45 and the board is correctly still lifted. This is
       the single most important test in the file -- it is why tilt_feed divides
       by |a| instead of thresholding az directly. */
    ASSERT_EQ(TILT_EV_NONE, feed(&s, 0.0f, 2.0f, 1.0f, &t, 2000, &n));
    ASSERT_EQ(0, n);
    PASS();
}

TEST freefall_samples_are_discarded(void) {
    tilt_state_t s; tilt_init(&s);
    uint32_t t = 0; int n = 0;
    feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 500, &n);       /* primed flat */
    /* |a| = 0.05, below TILT_MAG_MIN: ignored, zone untouched. */
    ASSERT_EQ(TILT_EV_NONE, feed(&s, 0.0f, 0.0f, 0.05f, &t, 2000, &n));
    ASSERT_EQ(0, n);
    /* State is intact: a genuine lift still reports normally afterwards. */
    ASSERT_EQ(TILT_EV_LIFTED, feed(&s, UP_X, UP_Y, UP_Z, &t, 1000, &n));
    ASSERT_EQ(1, n);
    PASS();
}

TEST flat_again_reports_once(void) {
    tilt_state_t s; tilt_init(&s);
    uint32_t t = 0; int n = 0;
    feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 500, &n);
    ASSERT_EQ(TILT_EV_LIFTED, feed(&s, UP_X, UP_Y, UP_Z, &t, 1000, &n));
    n = 0;
    ASSERT_EQ(TILT_EV_FLAT, feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 2000, &n));
    ASSERT_EQ(1, n);
    PASS();
}

TEST first_sample_inside_the_band_defers_priming(void) {
    tilt_state_t s; tilt_init(&s);
    uint32_t t = 0; int n = 0;
    /* 35 deg from the very first sample: there is no zone to adopt yet. */
    feed(&s, 0.0f, 0.574f, 0.819f, &t, 500, &n);
    ASSERT_EQ(0, n);
    /* Settling flat primes silently -- it must not look like a transition. */
    ASSERT_EQ(TILT_EV_NONE, feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 2000, &n));
    ASSERT_EQ(0, n);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(primes_silently_then_lifts_once);
    RUN_TEST(no_event_before_the_hold_elapses);
    RUN_TEST(returning_to_flat_mid_hold_cancels);
    RUN_TEST(dead_band_holds_the_zone);
    RUN_TEST(motion_is_rejected_by_normalization);
    RUN_TEST(freefall_samples_are_discarded);
    RUN_TEST(flat_again_reports_once);
    RUN_TEST(first_sample_inside_the_band_defers_priming);
    GREATEST_MAIN_END();
}
```

- [ ] **Step 3: Swap the test build list**

In `tests/CMakeLists.txt`, replace line 19:

```cmake
add_core_test(test_gestures ${CORE}/gestures.c)
```

with:

```cmake
add_core_test(test_tilt ${CORE}/tilt.c)
```

- [ ] **Step 4: Run the tests to verify they fail**

Run: `powershell -File tools/test.ps1`

Expected: **CMake fails at configure time** — `Cannot find source file: .../src/core/tilt.c`, because the build list now names a file that does not exist. That is the correct failure at this point; do not proceed until you have seen it.

- [ ] **Step 5: Write the implementation**

Create `src/core/tilt.c`:

```c
#include "tilt.h"
#include <math.h>

void tilt_init(tilt_state_t *t) {
    tilt_state_t z = {0};
    *t = z;
}

tilt_event_t tilt_feed(tilt_state_t *t, float ax, float ay, float az, uint32_t now_ms) {
    float mag = sqrtf(ax*ax + ay*ay + az*az);
    if (mag < TILT_MAG_MIN) return TILT_EV_NONE;   /* freefall, or a garbled read */
    float c = az / mag;                            /* cos of the tilt from level */

    /* Hysteresis: inside the band the committed zone stands unchanged. */
    int8_t target = t->zone;
    if      (c >= TILT_FLAT_COS) target = +1;
    else if (c <= TILT_LIFT_COS) target = -1;

    if (!t->primed) {
        /* Adopt the board's current orientation without reporting it, so
           switching the feature on mid-session cannot fire a spurious pause or
           resume. A first sample inside the dead band has nothing to adopt --
           stay unprimed and wait for one that does. */
        if (target == 0) return TILT_EV_NONE;
        t->primed = true;
        t->zone = target;
        return TILT_EV_NONE;
    }

    if (target == t->zone) { t->cand = 0; return TILT_EV_NONE; }

    if (t->cand != target) { t->cand = target; t->cand_since_ms = now_ms; return TILT_EV_NONE; }
    if ((int32_t)(now_ms - t->cand_since_ms) < (int32_t)TILT_HOLD_MS) return TILT_EV_NONE;

    t->zone = target;
    t->cand = 0;
    return (target == +1) ? TILT_EV_FLAT : TILT_EV_LIFTED;
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `powershell -File tools/test.ps1`

Expected: `100% tests passed, 0 tests failed out of 10`, with `test_tilt` among them and `test_gestures` gone. Zero compiler warnings.

- [ ] **Step 7: Delete the superseded classifier and swap the two remaining build lists**

```bash
git rm src/core/gestures.h src/core/gestures.c tests/test_gestures.c
```

In `CMakeLists.txt` (root), replace `    src/core/gestures.c` on line 31 with:

```cmake
    src/core/tilt.c
```

In `src/sim/CMakeLists.txt`, replace `    ${CMAKE_CURRENT_SOURCE_DIR}/../core/gestures.c` on line 27 with:

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/tilt.c
```

- [ ] **Step 8: Verify nothing still references the old module**

Run: `grep -rn "gestures\|gesture_" --include=*.c --include=*.h --include=*.txt src tests CMakeLists.txt`

Expected: **no output.** If anything matches, fix it before committing.

- [ ] **Step 9: Verify the simulator still builds**

Run: `cmake --build build-sim`

Expected: links cleanly. (Nothing consumes `tilt.c` yet — this only proves the build-list swap is correct.)

- [ ] **Step 10: Commit**

```bash
git add src/core/tilt.h src/core/tilt.c tests/test_tilt.c tests/CMakeLists.txt CMakeLists.txt src/sim/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(core): add the tilt orientation gate, replacing core/gestures

core/tilt.c answers one question -- is the board lying face-up and
level? -- and emits only the two edge transitions. Hysteresis (0.87
entering flat, 0.77 leaving) plus a 600 ms hold keeps a bump or a reach
across the desk from counting, and thresholding az/|a| rather than az
rejects motion: a 2 g jolt can push raw az past the flat threshold while
the board is nowhere near level.

The first usable sample primes the zone silently, so switching the
feature on mid-session cannot fire a spurious pause or resume.

core/gestures.c is deleted rather than extended. It has had no consumer
since Plan A; it structurally cannot detect "lifted" (held upright at
az~0 neither of its thresholds fires, so `face` keeps a stale value); and
it sets face_reported unconditionally while emitting the flip only if no
shake fired in the same sample, so a real flip -- which easily exceeds
its jerk threshold -- could be consumed permanently.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Real `hal_imu()` on both HALs

The HAL is the hardware seam, so nothing here is host-testable; correctness is established by the two builds plus the simulator smoke test in Task 4.

**Files:**
- Modify: `src/hal/hal_target.c` (include block, `hal_init`, `hal_imu`, `hal_caps`)
- Modify: `src/hal/hal_sim.c` (include block, tilt state, `key_watch`, `hal_imu`)

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `bool hal_imu(float *ax, float *ay, float *az)` now returns real data (device) or a keyboard-driven fake (sim), and `hal_caps().imu` reflects whether the BMI323 came up. Both are already declared in `src/hal/hal.h:44` and `:52` — **do not change `hal.h`.**

- [ ] **Step 1: Add the BMI323 include and its state flag on the device**

In `src/hal/hal_target.c`, next to the existing `#include "sensors/opt4001.h"` (line 8), add:

```c
#include "sensors/bmi323.h"
```

`wilibsp/bsp/CMakeLists.txt:33` already compiles `sensors/bmi323.c` into the BSP library, so **no CMake change is needed.**

Find the existing `static bool s_light;` declaration and add alongside it:

```c
static bool s_imu;
```

- [ ] **Step 2: Bring the IMU up in `hal_init`**

In `hal_init`, immediately after the existing line:

```c
    s_light = opt4001_init();
```

add:

```c
    /* Same I2C1 bus as the OPT4001 above and the codec's control registers
       below. bmi323_init() DIAGs its own chipid check. */
    s_imu = bmi323_init();
```

- [ ] **Step 3: Implement `hal_imu` on the device**

Replace this line in `src/hal/hal_target.c`:

```c
bool hal_imu(float *ax, float *ay, float *az) { (void)ax;(void)ay;(void)az; return false; }  /* Plan C */
```

with:

```c
/* Accelerometer only. The BSP burst-reads all six axes in one transaction; the
   gyro is not worth forking the driver to skip, and the tilt gate needs nothing
   but the gravity vector. */
bool hal_imu(float *ax, float *ay, float *az) {
    if (!s_imu) return false;
    bmi323_reading_t r;
    if (!bmi323_read(&r)) return false;
    *ax = r.ax; *ay = r.ay; *az = r.az;
    return true;
}
```

- [ ] **Step 4: Report the capability**

In `hal_caps()`, change `.imu=false` to `.imu=s_imu`, giving:

```c
hal_caps_t hal_caps(void) {
    hal_caps_t c = { .radio=false,.imu=s_imu,.light=s_light,.audio=s_audio_ok,
                     .buttons=true,.leds=true,.dvi=s_dvi };
    return c;
}
```

- [ ] **Step 5: Add the fake tilt angle to the simulator**

In `src/hal/hal_sim.c`, add to the include block (after `#include <string.h>`):

```c
#include <math.h>
```

Directly below the existing `static float s_lux = 300.0f;` line and its comment, add:

```c
/* fake board tilt, degrees away from face-up level; - / = tilt it, same idea.
   Stepping rather than snapping flat<->upright means the simulator actually
   exercises the hysteresis band and the hold timer in core/tilt.c. */
static int s_tilt_deg = 0;
```

- [ ] **Step 6: Wire the two keys**

In `key_watch`, after the two existing `LEFTBRACKET`/`RIGHTBRACKET` lux lines, add:

```c
        if (e->key.keysym.scancode == SDL_SCANCODE_MINUS)  s_tilt_deg = (s_tilt_deg > 15) ? s_tilt_deg - 15 : 0;
        if (e->key.keysym.scancode == SDL_SCANCODE_EQUALS) s_tilt_deg = (s_tilt_deg < 75) ? s_tilt_deg + 15 : 90;
```

`MINUS` and `EQUALS` are unused by `map_key`, so they are not queued as buttons.

- [ ] **Step 7: Implement `hal_imu` in the simulator**

Replace this line in `src/hal/hal_sim.c`:

```c
bool hal_imu(float *ax, float *ay, float *az) { *ax=0;*ay=0;*az=1.0f; return true; }
```

with:

```c
bool hal_imu(float *ax, float *ay, float *az) {
    float rad = (float)s_tilt_deg * 3.14159265f / 180.0f;
    *ax = sinf(rad); *ay = 0.0f; *az = cosf(rad);
    return true;
}
```

- [ ] **Step 8: Verify the simulator builds**

Run: `cmake --build build-sim`

Expected: compiles and links with zero warnings.

- [ ] **Step 9: Verify the device builds**

Run: `powershell -File tools/build.ps1 -Clean`

Expected: zero warnings, no RAM overflow, `wilidoro.uf2` produced. Note the reported region sizes — this task adds only a few hundred bytes.

- [ ] **Step 10: Commit**

```bash
git add src/hal/hal_target.c src/hal/hal_sim.c
git commit -m "$(cat <<'EOF'
feat(hal): make hal_imu real on both HALs

Device: bring the BMI323 up in hal_init next to the OPT4001 -- same I2C1
bus -- and wrap bmi323_read(), keeping the three accelerometer axes and
discarding the gyro the BSP's burst read fetches anyway. hal_caps().imu
now reports whether the part answered.

Simulator: - and = tilt a fake board in 15 degree steps, clamped 0..90
and starting flat, exactly as [ and ] already vary fake lux. Stepping
rather than snapping between two orientations means the simulator
exercises the hysteresis band and the hold timer for real.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: The `tilt_pause` setting and its Settings row

**Files:**
- Modify: `src/app/app_model.h` (add the field), `src/app/app_model.c` (default it)
- Modify: `tests/test_app_model.c` (assert the default)
- Modify: `src/ui/screen_settings.c` (row, value text, Default-key re-prime)

**Interfaces:**
- Consumes: `app_tilt_apply()` from Task 4. **Task 3 references a function Task 4 defines**, so `src/ui/screen_settings.c` will not link until Task 4 is done — that is expected, and Step 7 below is a compile-only check. `app_tilt_apply` takes no arguments and returns `void`.
- Produces: `bool app_settings_t.tilt_pause`, default `false`. Task 4 reads it as `s_app.settings.tilt_pause`.

- [ ] **Step 1: Write the failing test**

In `tests/test_app_model.c`, add one line to the existing `settings_defaults_are_classic_pomodoro` test, immediately after `ASSERT(s.dvi_on);`:

```c
    ASSERT_FALSE(s.tilt_pause);   /* opt-in: setting the board down must not surprise you */
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `powershell -File tools/test.ps1`

Expected: the build fails compiling `test_app_model.c` — `'app_settings_t' has no member named 'tilt_pause'`.

- [ ] **Step 3: Add the field and its default**

In `src/app/app_model.h`, inside `app_settings_t`, immediately after the `bool dvi_on;` line:

```c
    bool     tilt_pause;   /* IMU: lift or tilt the board to pause a focus session */
```

In `src/app/app_model.c`, inside `app_settings_defaults`, immediately after the `s->dvi_on = true;` line:

```c
    s->tilt_pause = false;
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `powershell -File tools/test.ps1`

Expected: `100% tests passed, 0 tests failed out of 10`.

- [ ] **Step 5: Add the Settings row**

All four edits are in `src/ui/screen_settings.c`.

Add to the include block, after `#include "theme.h"`:

```c
#include "hal.h"
```

(`src/ui/screen_timer.c:4` already does this, so the include path is known good.)

Extend the statics on line 10 with `s_val_tilt`:

```c
static lv_obj_t *s_val_focus, *s_val_short, *s_val_long, *s_val_vol, *s_val_beacon, *s_val_theme, *s_val_dvi, *s_val_tilt;
```

In `refresh_values`, after the existing `lv_label_set_text(s_val_dvi, ...)` line:

```c
    /* "no imu" rather than "off" when the BMI323 never came up, so a dead
       sensor is visible instead of a toggle that silently does nothing. */
    lv_label_set_text(s_val_tilt, hal_caps().imu ? (s->tilt_pause?"on":"off") : "no imu");
```

Extend the setting enum on line 25 with `SET_TILT`:

```c
enum { SET_FOCUS=1, SET_SHORT, SET_LONG, SET_VOL, SET_BEACON, SET_THEME, SET_DVI, SET_TILT };
```

- [ ] **Step 6: Handle the row and create it**

In `adj_event`, after the existing `case SET_DVI:` line:

```c
        case SET_TILT:  s->tilt_pause = !s->tilt_pause; app_tilt_apply(); break;
```

In `screen_settings_create`, after the existing `s_val_dvi = add_row("DVI output", SET_DVI);` line:

```c
    s_val_tilt   = add_row("Tilt to pause", SET_TILT);
```

In `screen_settings_softkey`, the `col==2` ("Default") branch currently reads:

```c
    else if (col==2) { app_settings_defaults(&a->settings); refresh_values(); screen_timer_apply_theme(); app_dvi_apply(); }
```

Add `app_tilt_apply()` to it:

```c
    else if (col==2) { app_settings_defaults(&a->settings); refresh_values(); screen_timer_apply_theme(); app_dvi_apply(); app_tilt_apply(); }
```

This is the same class of bug as the "Default desyncs `dvi_on`" regression recorded as item 7 of the DVI checklist in `docs/hardware-notes.md`: pressing Default resets `tilt_pause` in the struct, so the gate's primed zone must be re-adopted to match.

- [ ] **Step 7: Verify it compiles**

Run: `cmake --build build-sim`

Expected: `screen_settings.c` **compiles** clean, then the **link fails** with `undefined reference to 'app_tilt_apply'`. That is the expected state — Task 4 defines it. If you see a *compile* error in `screen_settings.c`, fix that before continuing.

- [ ] **Step 8: Commit**

```bash
git add src/app/app_model.h src/app/app_model.c tests/test_app_model.c src/ui/screen_settings.c
git commit -m "$(cat <<'EOF'
feat(settings): add the tilt_pause setting and its row

Defaults to false -- setting the board down should not silently start
gating a session until you ask for it.

The row reads "no imu" instead of "off" when hal_caps().imu is false, so
a BMI323 that never answered is visible rather than presenting a toggle
that does nothing. The Default softkey now re-applies the gate too, the
same fix the DVI toggle needed when Default reset dvi_on behind it.

Does not link on its own: app_tilt_apply() arrives with the app wiring.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: App wiring — one 100 ms sensor timer, and the pause/resume rule

Completes the feature. The lux poll moves out of `tick_cb` so that a single timer owns every I2C1 sensor read.

**Files:**
- Modify: `src/app/app.h` (declare `app_tilt_apply`)
- Modify: `src/app/app.c` (include, state, `app_tilt_apply`, `sensor_cb`, remove the lux block from `tick_cb`, `app_init`)
- Modify: `docs/hardware-notes.md` (tunables + on-device checklist)

**Interfaces:**
- Consumes: `tilt_init` / `tilt_feed` / `tilt_event_t` / `tilt_state_t` from Task 1; `hal_imu` from Task 2; `app_settings_t.tilt_pause` from Task 3. Also the pre-existing `pomodoro_pause(pomodoro_t*, uint32_t)`, `pomodoro_resume(pomodoro_t*, uint32_t)`, `pm_state_t` values `PM_FOCUS` / `PM_PAUSED`, and `pomodoro_t.resume_state` (`src/core/pomodoro.h`).
- Produces: `void app_tilt_apply(void)`, which Task 3's `screen_settings.c` already calls.

- [ ] **Step 1: Declare the seam**

In `src/app/app.h`, after the existing `void app_dvi_apply(void);` line:

```c
void   app_tilt_apply(void);         /* re-prime the tilt gate after tilt_pause changes */
```

- [ ] **Step 2: Include the gate and hold its state**

In `src/app/app.c`, add to the include block after `#include "dvi_view.h"`:

```c
#include "tilt.h"
```

Next to the existing `static dim_state_t s_dim;` declaration, add:

```c
static tilt_state_t s_tilt;
```

- [ ] **Step 3: Implement `app_tilt_apply`**

In `src/app/app.c`, immediately after the existing `app_dvi_apply` function:

```c
/* Re-prime the gate so enabling the feature adopts the board's current
   orientation instead of reporting it as a fresh transition. Called both when
   the toggle flips and when the Default softkey rewrites the settings struct. */
void app_tilt_apply(void) { tilt_init(&s_tilt); }
```

- [ ] **Step 4: Move the lux poll out of `tick_cb`**

In `tick_cb`, delete this entire block (currently `src/app/app.c:116-125`):

```c
    /* auto-dim: poll lux at ~2 Hz through the core dimming curve */
    if (now >= s_next_lux) {
        float lux;
        if (hal_lux(&lux)) {
            uint8_t pct = dim_apply(&s_dim, lux);
            hal_backlight(pct);
            hal_led_brightness((uint8_t)((uint32_t)dim_led_brightness(pct) * LED_BRIGHT_MAX / 255));
        }
        s_next_lux = now + 500;
    }
```

Leave the `/* per-theme LED pattern */` line that follows it, and everything after, untouched.

- [ ] **Step 5: Add `sensor_cb`**

In `src/app/app.c`, insert this immediately **before** `static void tick_cb(lv_timer_t *t)`:

```c
/* 100 ms cadence, and the sole owner of I2C1 sensor reads: the OPT4001, the
   BMI323 and the NAU88C10's control registers all share that bus, so one poller
   keeps its traffic predictable instead of two independent ones interleaving.
   The IMU is read every call -- 6 samples per TILT_HOLD_MS window, ~0.35 ms of
   bus time each, so ~0.4 % duty -- and the light sensor every fifth. With
   tilt_pause off the IMU is not touched at all. */
static void sensor_cb(lv_timer_t *t) {
    (void)t;
    uint32_t now = hal_now_ms();

    float ax, ay, az;
    if (s_app.settings.tilt_pause && hal_imu(&ax, &ay, &az)) {
        /* Edge-triggered deliberately: acting on the transition rather than on
           the standing orientation is what stops a manual Pause taken while the
           board is flat from being instantly undone. Breaks are never gated --
           you are meant to walk away from those -- and idle is never started.
           pomodoro_pause/resume are already guarded no-ops outside their valid
           states; these state checks are what confine the rule to focus. */
        switch (tilt_feed(&s_tilt, ax, ay, az, now)) {
            case TILT_EV_LIFTED:
                if (s_app.pomo.state == PM_FOCUS) {
                    pomodoro_pause(&s_app.pomo, now);
                    app_sound(SND_BLIP);
                }
                break;
            case TILT_EV_FLAT:
                if (s_app.pomo.state == PM_PAUSED && s_app.pomo.resume_state == PM_FOCUS) {
                    pomodoro_resume(&s_app.pomo, now);
                    app_sound(SND_BLIP);
                }
                break;
            case TILT_EV_NONE:
                break;
        }
    }

    /* auto-dim: poll lux at ~2 Hz through the core dimming curve */
    if ((int32_t)(now - s_next_lux) >= 0) {
        float lux;
        if (hal_lux(&lux)) {
            uint8_t pct = dim_apply(&s_dim, lux);
            hal_backlight(pct);
            hal_led_brightness((uint8_t)((uint32_t)dim_led_brightness(pct) * LED_BRIGHT_MAX / 255));
        }
        s_next_lux = now + 500;
    }
}
```

Note the deadline test is now wrap-safe (`(int32_t)(now - s_next_lux) >= 0`), matching every other deadline in this file; the old `now >= s_next_lux` would have stalled the auto-dim for 500 ms at the 49-day `hal_now_ms` rollover.

- [ ] **Step 6: Initialise and start the timer**

In `app_init`, immediately after the existing `dim_init(&s_dim, 100.0f);` line:

```c
    tilt_init(&s_tilt);
```

And immediately after the existing `lv_timer_create(sound_cb, 20, NULL);` line:

```c
    lv_timer_create(sensor_cb, 100, NULL);
```

- [ ] **Step 7: Verify the simulator builds and links**

Run: `cmake --build build-sim`

Expected: compiles **and links** clean this time — `app_tilt_apply` now exists.

- [ ] **Step 8: Verify the host suite still passes**

Run: `powershell -File tools/test.ps1`

Expected: `100% tests passed, 0 tests failed out of 10`.

- [ ] **Step 9: Exercise it in the simulator**

Run: `./build-sim/wilidoro_sim.exe` (or `build-sim/wilidoro_sim`).

Walk this sequence and confirm each observation before moving on:

1. Press `B` (red softkey, column 4 = "Menu") to reach Settings, scroll to the last row, **Tilt to pause**, and press its `+`. It reads `on`. Press `Z` (column 0 = "Back").
2. Press `Z` (column 0 = "Start") on the timer face. The countdown runs.
3. Press `=` six times to reach 90°. Within ~600 ms the timer shows **PAUSED**.
4. Press `-` six times back to 0°. Within ~600 ms it **resumes** from where it stopped, not from the top.
5. **The load-bearing check:** with the board flat (0°) and running, press `Z` (column 0 = "Pause") manually. It must **stay** paused — the gate must not resume it. This is what edge-triggering buys, and it is the single easiest thing to get wrong. Press `Z` again ("Resume") before continuing.
6. Press `=` twice only (30°, inside the dead band). Nothing happens — no pause.
7. Back in Settings, set **Tilt to pause** to `off`. Tilting now does nothing.
8. Press the **Default** softkey, then tilt: still nothing (Default resets `tilt_pause` to false), and no spurious pause fires from the re-prime.

- [ ] **Step 10: Verify the device builds**

Run: `powershell -File tools/build.ps1 -Clean`

Expected: zero warnings, no RAM overflow, `wilidoro.uf2` produced. Compare the reported RAM figures against the ~81 KB of headroom recorded in `docs/hardware-notes.md`; this plan adds a `tilt_state_t` (12 bytes) and one LVGL timer.

- [ ] **Step 11: Document the tunables and the on-device checklist**

Append to `docs/hardware-notes.md`, immediately before the final `---` separator and its "Why this note isn't in the BSP" paragraph:

```markdown
## Tilt to pause — thresholds and hold

*Not yet hardware-verified. The values below are reasoned defaults, chosen at
the desk and confirmed only in the simulator.*

The board lying face-up and level is the running orientation; lift or tilt it and
a focus session pauses. `src/core/tilt.c` thresholds `az/‖a‖` — the cosine of the
tilt away from level — rather than raw `az`, because a 2 g jolt can push raw `az`
past the flat threshold while the board is nowhere near level.

| tunable | value | meaning |
|---|---|---|
| `TILT_FLAT_COS` | 0.87 | enter flat, within ~29.5° of level |
| `TILT_LIFT_COS` | 0.77 | leave flat, beyond ~39.7° |
| `TILT_HOLD_MS` | 600 | the new zone must persist this long to count |
| `TILT_MAG_MIN` | 0.30 | below this `‖a‖` the sample is discarded |

The 10 Hz `sensor_cb` gives 6 samples per hold window. These are bench-comfort
choices in the same spirit as `LED_BRIGHT_MAX = 40` and `TONE_AMP_CAP = 160`, and
they belong here rather than upstream in the BSP. If a lift is missed, lower
`TILT_LIFT_COS`; if the desk shaking pauses a session, raise `TILT_HOLD_MS`.

**I2C1 is shared** by the BMI323 (0x68), the OPT4001, and the NAU88C10's control
registers. A single 100 ms `sensor_cb` in `src/app/app.c` owns every sensor read
on it — the IMU each call, lux every fifth — rather than two independent pollers.
All of it runs from LVGL timers on core 0, so this is a time-budget arrangement,
not a lock. A `bmi323_read` is 1 byte written plus 14 read, ~0.35 ms of bus time.
With the setting off, `hal_imu` is never called and the bus sees no extra traffic.

### On-device tilt checklist

None of this has been run. **Ask before flashing.**

1. RTT at boot reports `bmi323: chipid=0x0043 ok`. If it says `??`, the part did
   not answer and Settings will show `no imu` — check that before suspecting the
   gate logic.
2. Settings shows **Tilt to pause** as `off`, and switching it to `on` sticks.
3. With it on and a focus session running, lifting the board off the desk pauses
   within ~600 ms and blips.
4. Setting it back down flat resumes from the same remaining time, not from the
   top.
5. A manual Pause taken while the board is flat is **not** undone by the gate.
6. Resting it on a shallow stand (~35°, inside the dead band) neither pauses nor
   resumes, and does not oscillate.
7. With the setting off, tilting does nothing at all.
8. A break is never paused by tilting, only focus.
9. Auto-dim still tracks the room after the lux poll moved from `tick_cb` into
   `sensor_cb` — cover the sensor and confirm the backlight and LEDs drop.
```

- [ ] **Step 12: Commit**

```bash
git add src/app/app.h src/app/app.c docs/hardware-notes.md
git commit -m "$(cat <<'EOF'
feat(app): gate focus on the board lying flat

A new 100 ms sensor_cb becomes the sole owner of I2C1 sensor reads --
the IMU every call, lux every fifth -- so the OPT4001 and the BMI323
share one predictable cadence instead of two independent pollers on a
bus they also share with the codec's control registers. The lux poll
moves out of the accreting tick_cb, and its deadline becomes wrap-safe
like every other one in the file.

The rule acts on transitions, never on the standing orientation, so a
manual Pause taken while the board is flat is not instantly undone.
Lifting pauses only PM_FOCUS; setting down resumes only a session that
was paused out of focus. Breaks are never gated and idle is never
started.

Records the thresholds and an on-device checklist in hardware-notes.md,
marked plainly as simulator-only so far.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Definition of Done

- Host suite: 10 binaries, 10/10 passing, zero warnings.
- Device build: clean, zero warnings, no RAM overflow.
- Simulator: the nine-step walkthrough in Task 4 Step 9 all confirmed, especially step 5.
- `grep -rn "gestures" src tests CMakeLists.txt` returns nothing.
- Hardware verification is **deliberately not** part of this plan's done — it needs the user's explicit go-ahead, and the checklist in `docs/hardware-notes.md` is marked unverified until then.
