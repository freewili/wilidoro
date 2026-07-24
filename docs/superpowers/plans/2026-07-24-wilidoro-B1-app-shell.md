# Wilidoro Plan B1 — App Shell, Navigation & Functional Screens — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the Plan A blank-screen firmware into a fully usable Pomodoro timer — a controller that drives `core/pomodoro` from real input, with three navigable screens (Timer, Settings, Nearby) in a single neutral look — running in the SDL simulator and on-device via touch and the physical buttons.

**Architecture:** A thin `hal/` implementation for each target (SDL host + FreeWili device) sits under a pure, host-tested `app/` model (settings + neighbor table) and an LVGL `app/` controller that owns the `pomodoro_t`, builds the screens, runs a periodic tick timer, and routes input (LVGL touch + physical buttons) to per-screen actions. Presentation stays deliberately neutral; Plan B2 refactors the timer face behind a theme interface and adds the three skins.

**Tech Stack:** C11, Pico SDK 2.2.0, LVGL 9.2.2, wilibsp BSP (`uartkbd` buttons, `ft6336` touch, `ws2812` LEDs, `bl_pwm`), SDL2 (simulator), CTest host tests.

## Global Constraints

Every task's requirements implicitly include these. Values verbatim from the design spec and the wilibsp/LVGL research.

- **Physical buttons (FreeWili 2 14-button coprocessor — DONE, driven by wilibsp `bsp/input/uartkbd`).** There is **no A/B/X/Y**. The real buttons are: D-pad `UP/DOWN/LEFT/RIGHT/CENTER`; five softkeys below the screen `GREY/YELLOW/GREEN/BLUE/RED`; and discrete `HOME/OK/CANCEL/PAGE`. The five colored softkeys correspond 1:1 to the on-screen softkey bar (grey=leftmost … red=rightmost).
- **Softkey action map** (both the on-screen touch buttons and the colored physical buttons trigger these, by column index 0..4 = grey,yellow,green,blue,red):
  - Timer screen, running (FOCUS/BREAK): `Pause/Resume · Skip · +5 min · Nearby · Menu`
  - Timer screen, IDLE: `Start · — · — · Nearby · Menu`
  - Timer screen, ALARM (focus ended): `Dismiss · — · — · — · Menu` (Dismiss = acknowledge → break)
  - Settings screen: `Back · — · Default · — · Save` (edits via touch ± and D-pad)
  - Nearby screen: `Back · — · — · — · —`
- **HAL is the only hardware seam.** `app/`, `ui/`, and `core/` never call wilibsp/SDL directly — only `hal_*`. `core/` purity (Plan A) is unchanged.
- **Deferred to Plan C (do NOT implement real drivers here):** LED patterns/animations, audio synthesis, live IMU gestures, ambient auto-dim, and CC1101 radio TX/RX. In Plan B1 the device HAL provides: real `now_ms`, real buttons, real touch (via existing `lvgl_port`), real backlight (full on), and a simple real LED phase color; it **stubs** audio (no-op), IMU (`false`), lux (`false`), and beacon (no-op TX, `false` RX). The sim HAL provides fakes for everything (incl. fake neighbors over `hal_beacon_rx`) so the whole UI is demoable on PC.
- **LVGL 9.2.2 specifics:** `LV_USE_FLOAT 0` → `lv_value_precise_t` is an **integer**; pass whole-number degrees to `lv_arc_set_bg_angles`/`lv_arc_set_angles`. Fonts available: `lv_font_montserrat_14/16/24/48` only. Colors always via `lv_color_hex()`. Use v9 API names (`lv_screen_load_anim`, `lv_obj_delete`, `lv_timer_delete`, `lv_event_get_target_obj`).
- **LED refresh quirk:** the first `ws2812_show()` after PIO start latches only pixel 0 — the device HAL must `ws2812_show()` on a periodic refresh, not once.
- **Diagnostics:** device uses `DIAG(...)` (RTT, no floats). Host/sim may use normal stdio in sim-only code.
- **Hardware access rule (project):** building firmware, host tests, and the SDL sim are free to run. Flashing/RTT/debug-probe/camera steps must NOT be run without asking the user first — on-device visual acceptance is a user-gated checkpoint.
- **Commit trailer (every commit):** `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

## Scope note

This is **Part 1 of Plan B**. It delivers a working, navigable, single-look timer. **Plan B2** (separate plan) refactors the timer face behind a `theme_t` interface and implements the Neon Arc / Tomato Arcade / Flip Clock skins + theme switching. The `app_settings_t.theme` field and a Settings "Theme" row are created here as inert scaffolding B2 activates.

## Verification model

- Pure logic (`app/app_model`) → host CTest, fully autonomous.
- HAL + controller + screens → **build gates** (sim `wilidoro_sim.exe` links + launches without crash; device `wilidoro.uf2` links, no SRAM overflow) plus the behavioral coverage from `app_model` tests. **Visual/interactive confirmation** (the sim window, or on-device) is a **user-gated checkpoint** — do not assume it; batch it for the user.

---

## File Structure

```
src/hal/
  hal.h              MODIFY  real button enum, hal_init/hal_pump, caps additions
  hal_sim.c          NEW     SDL host HAL (keyboard→buttons via event watch, fake sensors/neighbors, on-screen LED strip)
  hal_target.c       NEW     device HAL (uartkbd buttons, ws2812 LEDs, bl_pwm, stubs for audio/imu/lux/beacon)
src/app/
  app_model.h/.c     NEW     PURE host-tested: settings (+clamped adjusters), neighbor table (upsert/expire)
  app.h/.c           NEW     LVGL controller: owns pomodoro_t+settings+neighbors, builds screens, tick timer, input routing
src/ui/
  ui.h/.c            NEW     shared neutral palette + softkey-bar builder + helpers
  screen_timer.h/.c  NEW     neutral timer face (arc + MM:SS + state + session dots + softkey bar)
  screen_settings.h/.c NEW   settings list wired to app_settings adjusters
  screen_nearby.h/.c NEW     nearby list from neighbor table
src/target/main.c    MODIFY  replace blank-screen demo with hal_init + app_init + loop(hal_pump + lv_timer_handler)
src/sim/main.c       MODIFY  replace blank-screen demo with hal_init + app_init + loop
tests/
  test_app_model.c   NEW     settings clamping + neighbor upsert/expire/count
  CMakeLists.txt     MODIFY  add_core_test(test_app_model ...) — note: app_model.c lives in src/app, not src/core
CMakeLists.txt       MODIFY  add app/ui/hal_target sources + include dirs to wilidoro target
src/sim/CMakeLists.txt MODIFY add app/ui/hal_sim sources + include dirs to wilidoro_sim target
```

---

### Task 1: `hal.h` revision + `app_model` settings (TDD)

**Files:**
- Modify: `src/hal/hal.h`
- Create: `src/app/app_model.h`, `src/app/app_model.c`, `tests/test_app_model.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces `hal.h` (the full revised interface every later task and Plan C rely on) and the `app_settings_t` model + adjusters.

- [ ] **Step 1: Replace `src/hal/hal.h` with the revised interface**

```c
#ifndef WILIDORO_HAL_H
#define WILIDORO_HAL_H
#include <stdint.h>
#include <stdbool.h>
#include "beacon.h"

/* Called once at startup (before app_init) and once per main-loop iteration. */
void hal_init(void);
void hal_pump(void);           /* drain hardware input each loop iteration (device: uartkbd_task) */

uint32_t hal_now_ms(void);

/* FreeWili 2 physical buttons (14-button coprocessor). Event queue of PRESS edges. */
typedef enum {
    HAL_BTN_GREY = 0, HAL_BTN_YELLOW, HAL_BTN_GREEN, HAL_BTN_BLUE, HAL_BTN_RED, /* 5 softkeys below screen */
    HAL_BTN_UP, HAL_BTN_DOWN, HAL_BTN_LEFT, HAL_BTN_RIGHT, HAL_BTN_CENTER,      /* D-pad */
    HAL_BTN_HOME, HAL_BTN_OK, HAL_BTN_CANCEL, HAL_BTN_PAGE,
    HAL_BTN_COUNT
} hal_btn_t;
bool hal_next_button(hal_btn_t *out);  /* dequeue next press edge; false if none */

/* 16 LEDs (index 0..15). */
void hal_led_set(int i, uint8_t r, uint8_t g, uint8_t b);
void hal_led_brightness(uint8_t level);   /* 0..255 */
void hal_led_show(void);

/* Audio (Plan C implements; Plan B stubs on device). */
void hal_tone(uint16_t hz, uint16_t ms, uint8_t amp);
void hal_audio_idle(void);

/* Backlight 0..100. */
void hal_backlight(uint8_t pct);

/* Sensors (Plan C; Plan B device returns false). */
bool hal_imu(float *ax, float *ay, float *az);
bool hal_lux(float *lux);

/* Radio beacon (Plan C; Plan B device stubs; sim fakes). */
void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN]);
bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]);

/* Which features are live this build (crossed-out icons on the timer face). */
typedef struct { bool radio, imu, light, audio, buttons, leds; } hal_caps_t;
hal_caps_t hal_caps(void);
#endif
```

- [ ] **Step 2: Write the failing settings tests**

`tests/test_app_model.c`:
```c
#include "greatest.h"
#include "app_model.h"
#include <string.h>

TEST settings_defaults_are_classic_pomodoro(void) {
    app_settings_t s; app_settings_defaults(&s);
    ASSERT_EQ(25, s.focus_min);
    ASSERT_EQ(5,  s.short_min);
    ASSERT_EQ(15, s.long_min);
    ASSERT_EQ(4,  s.long_every);
    ASSERT_EQ(0,  s.theme);
    ASSERT(s.beacon_on);
    PASS();
}

TEST focus_clamps_between_5_and_60_step_5(void) {
    app_settings_t s; app_settings_defaults(&s);
    for (int i = 0; i < 20; i++) app_settings_adjust_focus(&s, +1);
    ASSERT_EQ(60, s.focus_min);              /* clamped high */
    for (int i = 0; i < 20; i++) app_settings_adjust_focus(&s, -1);
    ASSERT_EQ(5, s.focus_min);               /* clamped low */
    app_settings_adjust_focus(&s, +1);
    ASSERT_EQ(10, s.focus_min);              /* step of 5 */
    PASS();
}

TEST volume_clamps_0_100_step_10(void) {
    app_settings_t s; app_settings_defaults(&s);
    for (int i = 0; i < 20; i++) app_settings_adjust_volume(&s, -1);
    ASSERT_EQ(0, s.volume);
    for (int i = 0; i < 20; i++) app_settings_adjust_volume(&s, +1);
    ASSERT_EQ(100, s.volume);
    PASS();
}

TEST theme_cycles_0_1_2(void) {
    app_settings_t s; app_settings_defaults(&s);
    app_settings_cycle_theme(&s); ASSERT_EQ(1, s.theme);
    app_settings_cycle_theme(&s); ASSERT_EQ(2, s.theme);
    app_settings_cycle_theme(&s); ASSERT_EQ(0, s.theme);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(settings_defaults_are_classic_pomodoro);
    RUN_TEST(focus_clamps_between_5_and_60_step_5);
    RUN_TEST(volume_clamps_0_100_step_10);
    RUN_TEST(theme_cycles_0_1_2);
    /* neighbor-table tests appended in Task 2 */
    GREATEST_MAIN_END();
}
```
Add to `tests/CMakeLists.txt` (note the source path is `src/app`, not `${CORE}`):
```cmake
add_executable(test_app_model test_app_model.c ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/app_model.c)
target_include_directories(test_app_model PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CORE} ${CMAKE_CURRENT_SOURCE_DIR}/../src/app)
target_compile_options(test_app_model PRIVATE -Wall -Wextra)
target_link_libraries(test_app_model m)
add_test(NAME test_app_model COMMAND test_app_model)
```
(`${CORE}` is on the include path because `app_model.h` includes `beacon.h` for `beacon_msg_t`/`beacon_state_t` used by the neighbor table in Task 2.)

- [ ] **Step 3: Run — verify build failure**

Run: `powershell -File tools/test.ps1`
Expected: FAILS (`app_model.h` not found).

- [ ] **Step 4: Write `app_model.h` and the settings half of `app_model.c`**

`src/app/app_model.h`:
```c
#ifndef WILIDORO_APP_MODEL_H
#define WILIDORO_APP_MODEL_H
#include <stdint.h>
#include <stdbool.h>
#include "beacon.h"

#define APP_NAME_LEN    BEACON_NAME_LEN   /* 8 */
#define NEIGHBOR_MAX    8
#define NEIGHBOR_TTL_MS 60000u

typedef struct {
    uint16_t focus_min, short_min, long_min, long_every;
    uint8_t  volume;       /* 0..100 */
    bool     focus_tick;
    bool     beacon_on;
    char     name[APP_NAME_LEN];  /* space-padded ASCII, not NUL-terminated */
    uint8_t  theme;        /* 0..2; activated in Plan B2 */
} app_settings_t;

void app_settings_defaults(app_settings_t *s);
void app_settings_adjust_focus(app_settings_t *s, int delta);   /* clamp 5..60, step 5 */
void app_settings_adjust_short(app_settings_t *s, int delta);   /* clamp 1..30, step 1 */
void app_settings_adjust_long(app_settings_t *s, int delta);    /* clamp 5..60, step 5 */
void app_settings_adjust_volume(app_settings_t *s, int delta);  /* clamp 0..100, step 10 */
void app_settings_cycle_theme(app_settings_t *s);               /* 0->1->2->0 */

typedef struct {
    char           name[APP_NAME_LEN];
    beacon_state_t state;
    uint8_t        minutes_left;
    uint8_t        completed;
    uint32_t       last_seen_ms;
    bool           used;
} neighbor_t;

typedef struct { neighbor_t items[NEIGHBOR_MAX]; } neighbor_table_t;

void neighbor_table_init(neighbor_table_t *t);
void neighbor_upsert(neighbor_table_t *t, const beacon_msg_t *m, uint32_t now_ms);
void neighbor_expire(neighbor_table_t *t, uint32_t now_ms);
int  neighbor_count(const neighbor_table_t *t);
#endif
```
`src/app/app_model.c` (settings half — the neighbor half is added in Task 2):
```c
#include "app_model.h"
#include <string.h>

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void app_settings_defaults(app_settings_t *s) {
    s->focus_min = 25; s->short_min = 5; s->long_min = 15; s->long_every = 4;
    s->volume = 70; s->focus_tick = false; s->beacon_on = true; s->theme = 0;
    memcpy(s->name, "WILI    ", APP_NAME_LEN);
}
void app_settings_adjust_focus(app_settings_t *s, int delta) {
    s->focus_min = (uint16_t)clampi((int)s->focus_min + delta * 5, 5, 60);
}
void app_settings_adjust_short(app_settings_t *s, int delta) {
    s->short_min = (uint16_t)clampi((int)s->short_min + delta, 1, 30);
}
void app_settings_adjust_long(app_settings_t *s, int delta) {
    s->long_min = (uint16_t)clampi((int)s->long_min + delta * 5, 5, 60);
}
void app_settings_adjust_volume(app_settings_t *s, int delta) {
    s->volume = (uint8_t)clampi((int)s->volume + delta * 10, 0, 100);
}
void app_settings_cycle_theme(app_settings_t *s) { s->theme = (uint8_t)((s->theme + 1) % 3); }
```

- [ ] **Step 5: Run — verify green (4 settings tests)**

Run: `powershell -File tools/test.ps1`
Expected: `test_app_model` passes the 4 settings cases; whole suite green.

- [ ] **Step 6: Commit**

```bash
git add src/hal/hal.h src/app/app_model.h src/app/app_model.c tests/test_app_model.c tests/CMakeLists.txt
git commit -m "feat(app): revise HAL button model + app settings with tests"
```

---

### Task 2: `app_model` neighbor table (TDD)

**Files:**
- Modify: `src/app/app_model.c`, `tests/test_app_model.c`

**Interfaces:**
- Consumes: `app_model.h` types from Task 1, `beacon_msg_t`/`beacon_state_t` from `core/beacon.h`.
- Produces: `neighbor_table_init/upsert/expire/count` used by the Nearby screen and the controller.

- [ ] **Step 1: Add the failing neighbor tests**

Insert these tests into `tests/test_app_model.c` before `GREATEST_MAIN_DEFS();`, and add their `RUN_TEST(...)` lines in `main` after the settings ones:
```c
static beacon_msg_t mk(const char *n8, beacon_state_t st, uint8_t min, uint8_t done) {
    beacon_msg_t m; memcpy(m.name, n8, 8); m.state = st; m.minutes_left = min; m.completed = done;
    return m;
}

TEST upsert_adds_then_updates_by_name(void) {
    neighbor_table_t t; neighbor_table_init(&t);
    beacon_msg_t a = mk("JEN     ", BST_FOCUS, 12, 3);
    neighbor_upsert(&t, &a, 1000);
    ASSERT_EQ(1, neighbor_count(&t));
    beacon_msg_t a2 = mk("JEN     ", BST_BREAK, 4, 3);
    neighbor_upsert(&t, &a2, 2000);
    ASSERT_EQ(1, neighbor_count(&t));           /* same name -> update, not add */
    /* find JEN and check it updated */
    int found = -1;
    for (int i = 0; i < NEIGHBOR_MAX; i++)
        if (t.items[i].used && memcmp(t.items[i].name, "JEN     ", 8) == 0) found = i;
    ASSERT(found >= 0);
    ASSERT_EQ(BST_BREAK, t.items[found].state);
    ASSERT_EQ(4, t.items[found].minutes_left);
    ASSERT_EQ(2000, t.items[found].last_seen_ms);
    PASS();
}

TEST expire_drops_stale_keeps_fresh(void) {
    neighbor_table_t t; neighbor_table_init(&t);
    beacon_msg_t a = mk("OLD     ", BST_IDLE, 0, 1);
    beacon_msg_t b = mk("NEW     ", BST_FOCUS, 20, 2);
    neighbor_upsert(&t, &a, 1000);
    neighbor_upsert(&t, &b, 50000);
    neighbor_expire(&t, 1000 + NEIGHBOR_TTL_MS + 1);   /* OLD is stale, NEW is fresh */
    ASSERT_EQ(1, neighbor_count(&t));
    ASSERT(t.items[0].used ? memcmp(t.items[0].name,"NEW     ",8)==0 : true);
    PASS();
}

TEST upsert_evicts_oldest_when_full(void) {
    neighbor_table_t t; neighbor_table_init(&t);
    char nm[9] = "N0      ";
    for (int i = 0; i < NEIGHBOR_MAX; i++) { nm[1] = (char)('0'+i); beacon_msg_t m = mk(nm, BST_IDLE, 0, 0); neighbor_upsert(&t, &m, (uint32_t)(100+i)); }
    ASSERT_EQ(NEIGHBOR_MAX, neighbor_count(&t));
    beacon_msg_t extra = mk("EXTRA   ", BST_FOCUS, 5, 0);
    neighbor_upsert(&t, &extra, 100000);
    ASSERT_EQ(NEIGHBOR_MAX, neighbor_count(&t));       /* still full */
    /* N0 (oldest, last_seen 100) evicted; EXTRA present */
    bool has_extra = false, has_n0 = false;
    for (int i = 0; i < NEIGHBOR_MAX; i++) if (t.items[i].used) {
        if (memcmp(t.items[i].name,"EXTRA   ",8)==0) has_extra = true;
        if (memcmp(t.items[i].name,"N0      ",8)==0) has_n0 = true;
    }
    ASSERT(has_extra); ASSERT_FALSE(has_n0);
    PASS();
}
```

- [ ] **Step 2: Run — verify failure (link error: neighbor_* undefined)**

Run: `powershell -File tools/test.ps1`
Expected: FAILS to link (`undefined reference to neighbor_table_init` etc.).

- [ ] **Step 3: Append the neighbor implementation to `src/app/app_model.c`**

```c
void neighbor_table_init(neighbor_table_t *t) {
    for (int i = 0; i < NEIGHBOR_MAX; i++) t->items[i].used = false;
}

int neighbor_count(const neighbor_table_t *t) {
    int n = 0;
    for (int i = 0; i < NEIGHBOR_MAX; i++) if (t->items[i].used) n++;
    return n;
}

static void copy_from_msg(neighbor_t *dst, const beacon_msg_t *m, uint32_t now_ms) {
    memcpy(dst->name, m->name, APP_NAME_LEN);
    dst->state = m->state;
    dst->minutes_left = m->minutes_left;
    dst->completed = m->completed;
    dst->last_seen_ms = now_ms;
    dst->used = true;
}

void neighbor_upsert(neighbor_table_t *t, const beacon_msg_t *m, uint32_t now_ms) {
    /* 1) update existing by name */
    for (int i = 0; i < NEIGHBOR_MAX; i++)
        if (t->items[i].used && memcmp(t->items[i].name, m->name, APP_NAME_LEN) == 0) {
            copy_from_msg(&t->items[i], m, now_ms); return;
        }
    /* 2) use a free slot */
    for (int i = 0; i < NEIGHBOR_MAX; i++)
        if (!t->items[i].used) { copy_from_msg(&t->items[i], m, now_ms); return; }
    /* 3) evict the oldest (smallest last_seen_ms) */
    int oldest = 0;
    for (int i = 1; i < NEIGHBOR_MAX; i++)
        if (t->items[i].last_seen_ms < t->items[oldest].last_seen_ms) oldest = i;
    copy_from_msg(&t->items[oldest], m, now_ms);
}

void neighbor_expire(neighbor_table_t *t, uint32_t now_ms) {
    for (int i = 0; i < NEIGHBOR_MAX; i++)
        if (t->items[i].used && (now_ms - t->items[i].last_seen_ms) > NEIGHBOR_TTL_MS)
            t->items[i].used = false;
}
```

- [ ] **Step 4: Run — verify green (all 7 app_model tests + whole suite)**

Run: `powershell -File tools/test.ps1`
Expected: `test_app_model` passes all 7; ctest `100% tests passed` (now 6 test binaries).

- [ ] **Step 5: Commit**

```bash
git add src/app/app_model.c tests/test_app_model.c
git commit -m "feat(app): neighbor table (upsert/expire/evict) with tests"
```

---

### Task 3: `hal_sim.c` — SDL host HAL (+ sim CMake)

**Files:**
- Create: `src/hal/hal_sim.c`
- Modify: `src/sim/CMakeLists.txt`

**Interfaces:**
- Consumes: `hal.h` (Task 1), `beacon.h`.
- Produces: a full HAL for the simulator. Keyboard mapping: `Z X C V B` → GREY/YELLOW/GREEN/BLUE/RED; arrow keys + Return → D-pad UP/DOWN/LEFT/RIGHT + CENTER. Provides one fake neighbor via `hal_beacon_rx`.

- [ ] **Step 1: Write `src/hal/hal_sim.c`**

```c
#include "hal.h"
#include <SDL2/SDL.h>
#include <string.h>

/* --- button event queue, fed by an SDL event watch (snoops without consuming) --- */
#define QN 32
static volatile int q_head = 0, q_tail = 0;
static hal_btn_t q_buf[QN];
static void q_push(hal_btn_t b) { int n = (q_head + 1) % QN; if (n != q_tail) { q_buf[q_head] = b; q_head = n; } }

static int map_key(SDL_Scancode sc, hal_btn_t *out) {
    switch (sc) {
        case SDL_SCANCODE_Z: *out = HAL_BTN_GREY;   return 1;
        case SDL_SCANCODE_X: *out = HAL_BTN_YELLOW; return 1;
        case SDL_SCANCODE_C: *out = HAL_BTN_GREEN;  return 1;
        case SDL_SCANCODE_V: *out = HAL_BTN_BLUE;   return 1;
        case SDL_SCANCODE_B: *out = HAL_BTN_RED;    return 1;
        case SDL_SCANCODE_UP:     *out = HAL_BTN_UP;     return 1;
        case SDL_SCANCODE_DOWN:   *out = HAL_BTN_DOWN;   return 1;
        case SDL_SCANCODE_LEFT:   *out = HAL_BTN_LEFT;   return 1;
        case SDL_SCANCODE_RIGHT:  *out = HAL_BTN_RIGHT;  return 1;
        case SDL_SCANCODE_RETURN: *out = HAL_BTN_CENTER; return 1;
        default: return 0;
    }
}
static int SDLCALL key_watch(void *u, SDL_Event *e) {
    (void)u;
    if (e->type == SDL_KEYDOWN && e->key.repeat == 0) {
        hal_btn_t b; if (map_key(e->key.keysym.scancode, &b)) q_push(b);
    }
    return 1; /* keep event in queue for LVGL */
}

void hal_init(void) { SDL_AddEventWatch(key_watch, NULL); }
void hal_pump(void) { /* SDL is pumped by lv_timer_handler; nothing to do */ }

uint32_t hal_now_ms(void) { return SDL_GetTicks(); }

bool hal_next_button(hal_btn_t *out) {
    if (q_tail == q_head) return false;
    *out = q_buf[q_tail]; q_tail = (q_tail + 1) % QN; return true;
}

/* LEDs — store state; a visible on-screen strip is a Plan C nicety, so keep it minimal here. */
static uint8_t s_led_bri = 40;
void hal_led_set(int i, uint8_t r, uint8_t g, uint8_t b) { (void)i;(void)r;(void)g;(void)b; }
void hal_led_brightness(uint8_t level) { s_led_bri = level; }
void hal_led_show(void) { (void)s_led_bri; }

void hal_tone(uint16_t hz, uint16_t ms, uint8_t amp) { (void)hz;(void)ms;(void)amp; }
void hal_audio_idle(void) {}

void hal_backlight(uint8_t pct) { (void)pct; }

bool hal_imu(float *ax, float *ay, float *az) { *ax=0;*ay=0;*az=1.0f; return true; }
bool hal_lux(float *lux) { *lux = 300.0f; return true; }

/* Fake neighbor: emit one valid beacon frame ~every 4s so the Nearby screen has content. */
void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN]) { (void)wire; }
bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]) {
    static uint32_t last = 0; uint32_t now = SDL_GetTicks();
    if (now - last < 4000) return false;
    last = now;
    beacon_msg_t m; memcpy(m.name, "JEN     ", BEACON_NAME_LEN);
    m.state = BST_FOCUS; m.minutes_left = (uint8_t)(12 + (now/1000) % 5); m.completed = 3;
    beacon_pack(&m, wire);
    return true;
}

hal_caps_t hal_caps(void) { hal_caps_t c = { .radio=true,.imu=true,.light=true,.audio=true,.buttons=true,.leds=true }; return c; }
```
Note: `hal_beacon_rx`'s `wire` parameter is written (it's the out-buffer); `beacon_pack` fills it. This compiles under `-Wall -Wextra` because all params are used or `(void)`-cast.

- [ ] **Step 2: Add sources to `src/sim/CMakeLists.txt`**

Add `hal_sim.c`, the `app/`, and `ui/` sources to the `wilidoro_sim` executable and their include dirs. In `src/sim/CMakeLists.txt`, extend the `add_executable(wilidoro_sim ...)` source list with:
```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/../hal/hal_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/app_model.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/ui.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/screen_timer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/screen_settings.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/screen_nearby.c
```
and extend its `target_include_directories(...)` with:
```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/../app
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui
```
(`../hal` and `../core` are already on the include path from Plan A.)

- [ ] **Step 3: Verify hal_sim.c compiles standalone**

Run: `"C:/msys64/mingw64/bin/gcc.exe" -fsyntax-only -DWILIDORO_SIM=1 -Isrc/core -Isrc/hal -IC:/msys64/mingw64/include src/hal/hal_sim.c`
Expected: no output. (The sim CMake won't link yet — `app.c`/`ui/*.c` don't exist until Tasks 5–9 — so do NOT run the full sim build in this task; the syntax check is the gate here.)

- [ ] **Step 4: Commit**

```bash
git add src/hal/hal_sim.c src/sim/CMakeLists.txt
git commit -m "feat(hal): SDL host HAL (keyboard buttons, fake sensors + neighbor)"
```

---

### Task 4: `hal_target.c` — device HAL (+ device CMake)

**Files:**
- Create: `src/hal/hal_target.c`
- Modify: `CMakeLists.txt` (root)

**Interfaces:**
- Consumes: `hal.h`, wilibsp (`uartkbd`, `ws2812_driver`, `board.h`), Plan A `bl_pwm`.
- Produces: the device HAL. Buttons from `uartkbd`; LEDs from `ws2812`; backlight from `bl_pwm`; audio/imu/lux/beacon stubbed (caps report them false).

- [ ] **Step 1: Write `src/hal/hal_target.c`**

```c
#include "hal.h"
#include "fw2.h"
#include "input/uartkbd.h"
#include "input/uartkbd_parse.h"
#include "leds/ws2812_driver.h"
#include "leds/led_color.h"
#include "bl_pwm.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"

/* uartkbd_btn_t enum order matches hal_btn_t exactly (grey..page), so cast is safe. */
void hal_init(void) {
    uartkbd_init();
    ws2812_init(pio1, (uint)pio_claim_unused_sm(pio1, true), PIN_LED_DATA);
    ws2812_set_brightness(40);
    ws2812_clear();
    ws2812_show();
    bl_pwm_init();       /* backlight full-on; auto-dim is Plan C */
}
void hal_pump(void) { uartkbd_task(); }

uint32_t hal_now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

bool hal_next_button(hal_btn_t *out) {
    uartkbd_event_t ev;
    while (uartkbd_next_event(&ev)) {
        if (ev.pressed && (int)ev.btn < HAL_BTN_COUNT) { *out = (hal_btn_t)ev.btn; return true; }
    }
    return false;
}

void hal_led_set(int i, uint8_t r, uint8_t g, uint8_t b) {
    if (i < 0 || i >= WS2812_NUM_PIXELS) return;
    rgb_t c = { r, g, b }; ws2812_set_pixel((uint)i, c);
}
void hal_led_brightness(uint8_t level) { ws2812_set_brightness(level); }
void hal_led_show(void) { ws2812_show(); }

void hal_tone(uint16_t hz, uint16_t ms, uint8_t amp) { (void)hz;(void)ms;(void)amp; }  /* Plan C */
void hal_audio_idle(void) {}

void hal_backlight(uint8_t pct) { bl_pwm_set(pct); }

bool hal_imu(float *ax, float *ay, float *az) { (void)ax;(void)ay;(void)az; return false; }  /* Plan C */
bool hal_lux(float *lux) { (void)lux; return false; }                                        /* Plan C */

void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN]) { (void)wire; }                       /* Plan C */
bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]) { (void)wire; return false; }               /* Plan C */

hal_caps_t hal_caps(void) {
    hal_caps_t c = { .radio=false,.imu=false,.light=false,.audio=false,.buttons=true,.leds=true };
    return c;
}
```
Note: verify `uartkbd_btn_t` values (grey=0..page=13) match `hal_btn_t` (grey=0..page=13) — they do by construction. If the wilibsp header orders them differently, replace the cast with an explicit `switch`. Confirm the include paths (`input/uartkbd.h`) against the actual `wilibsp/bsp/input/` layout and adjust to match how `fw2.h` exposes them (it may already include them).

- [ ] **Step 2: Add sources to root `CMakeLists.txt`**

Extend `add_executable(wilidoro ...)` with:
```cmake
    src/hal/hal_target.c
    src/app/app_model.c
    src/app/app.c
    src/ui/ui.c
    src/ui/screen_timer.c
    src/ui/screen_settings.c
    src/ui/screen_nearby.c
```
and extend `target_include_directories(wilidoro PRIVATE ...)` with `src/app src/ui` (src/core, src/hal, src/target already present).

- [ ] **Step 3: Build the device firmware**

Run: `powershell -File tools/build.ps1 -Clean` (long timeout)
Expected: **This will FAIL to link** because `app.c`/`ui/*.c` don't exist yet — that's expected at this task. Instead, verify `hal_target.c` compiles by checking the build error is ONLY about missing `app_*`/`screen_*`/`ui_*` symbols, NOT about anything inside `hal_target.c`. If the compiler reports an error *inside `hal_target.c`* (bad include path, wrong `uartkbd` symbol), fix it against the real wilibsp headers and re-check. Paste the relevant compiler output into your report.
(Alternative focused check that avoids the link step: `"C:/msys64/mingw64/bin/gcc.exe"` cannot compile RP2350 code; rely on the CMake compile of the single TU — look for `hal_target.c.obj` being produced under `build/` even though the final link fails.)

- [ ] **Step 4: Commit**

```bash
git add src/hal/hal_target.c CMakeLists.txt
git commit -m "feat(hal): device HAL (uartkbd buttons, ws2812 LEDs, backlight; Plan C stubs)"
```

---

### Task 5: `app` controller skeleton + wire both mains (builds sim + device)

**Files:**
- Create: `src/app/app.h`, `src/app/app.c`, `src/ui/ui.h`, `src/ui/ui.c`, and minimal `src/ui/screen_timer.{h,c}`, `src/ui/screen_settings.{h,c}`, `src/ui/screen_nearby.{h,c}` (stubs that Tasks 6–8 flesh out)
- Modify: `src/target/main.c`, `src/sim/main.c`

**Interfaces:**
- Produces: `void app_init(void);` (builds the app after LVGL + hal are up) and the controller tick. Each screen exposes `lv_obj_t *screen_X_create(void);` and `void screen_X_update(void);` and `void screen_X_softkey(int col);` (col 0..4). The controller holds the shared `app_t`.

- [ ] **Step 1: Write `src/app/app.h`**

```c
#ifndef WILIDORO_APP_H
#define WILIDORO_APP_H
#include "pomodoro.h"
#include "app_model.h"

typedef enum { SCREEN_TIMER, SCREEN_SETTINGS, SCREEN_NEARBY } app_screen_t;

typedef struct {
    pomodoro_t       pomo;
    app_settings_t   settings;
    neighbor_table_t neighbors;
    app_screen_t     screen;
    bool             alarm_active;   /* focus ended, waiting for dismiss */
} app_t;

app_t *app(void);                    /* the single shared instance */
void   app_init(void);               /* build screens + start tick timer + load timer screen */
void   app_goto(app_screen_t s);     /* switch screens with a slide anim */
#endif
```

- [ ] **Step 2: Write `src/app/app.c` (controller + 200ms tick)**

```c
#include "app.h"
#include "hal.h"
#include "ui.h"
#include "screen_timer.h"
#include "screen_settings.h"
#include "screen_nearby.h"
#include "lvgl.h"

static app_t s_app;
app_t *app(void) { return &s_app; }

static lv_obj_t *s_scr[3];

static void route_softkey(int col) {
    switch (s_app.screen) {
        case SCREEN_TIMER:    screen_timer_softkey(col);    break;
        case SCREEN_SETTINGS: screen_settings_softkey(col); break;
        case SCREEN_NEARBY:   screen_nearby_softkey(col);   break;
    }
}

static void tick_cb(lv_timer_t *t) {
    (void)t;
    uint32_t now = hal_now_ms();

    /* advance the pomodoro; surface focus-end as an alarm state */
    pm_event_t ev = pomodoro_tick(&s_app.pomo, now);
    if (ev == PM_EV_FOCUS_ENDED) { s_app.alarm_active = true; hal_tone(880, 200, 200); }
    if (ev == PM_EV_BREAK_ENDED) { hal_tone(660, 120, 160); }

    /* physical buttons -> softkey columns (grey..red == cols 0..4) */
    hal_btn_t b;
    while (hal_next_button(&b)) {
        if (b <= HAL_BTN_RED) route_softkey((int)b);
        else if (b == HAL_BTN_CANCEL || b == HAL_BTN_HOME) app_goto(SCREEN_TIMER);
    }

    /* beacon rx -> neighbor table (Plan C makes tx/rx real; sim fakes rx) */
    uint8_t wire[BEACON_WIRE_LEN];
    if (hal_beacon_rx(wire)) { beacon_msg_t m; if (beacon_unpack(wire, &m)) neighbor_upsert(&s_app.neighbors, &m, now); }
    neighbor_expire(&s_app.neighbors, now);

    /* simple phase LED color (Plan C replaces with theme patterns) */
    uint8_t r=0,g=0,bl=0;
    if (s_app.pomo.state == PM_FOCUS)      { r=255; g=90; bl=40; }
    else if (s_app.pomo.state==PM_BREAK_SHORT||s_app.pomo.state==PM_BREAK_LONG){ r=40; g=200; bl=180; }
    for (int i=0;i<16;i++) hal_led_set(i, r, g, bl);
    hal_led_show();

    /* refresh the active screen */
    switch (s_app.screen) {
        case SCREEN_TIMER:    screen_timer_update();    break;
        case SCREEN_SETTINGS: screen_settings_update(); break;
        case SCREEN_NEARBY:   screen_nearby_update();   break;
    }
}

void app_goto(app_screen_t s) {
    s_app.screen = s;
    lv_screen_load_anim(s_scr[s], LV_SCR_LOAD_ANIM_FADE_IN, 150, 0, false);
}

void app_init(void) {
    app_settings_defaults(&s_app.settings);
    pm_config_t cfg = { .focus_min = s_app.settings.focus_min, .short_min = s_app.settings.short_min,
                        .long_min = s_app.settings.long_min, .long_every = s_app.settings.long_every };
    pomodoro_init(&s_app.pomo, cfg);
    neighbor_table_init(&s_app.neighbors);
    s_app.screen = SCREEN_TIMER; s_app.alarm_active = false;

    s_scr[SCREEN_TIMER]    = screen_timer_create();
    s_scr[SCREEN_SETTINGS] = screen_settings_create();
    s_scr[SCREEN_NEARBY]   = screen_nearby_create();
    lv_screen_load(s_scr[SCREEN_TIMER]);

    lv_timer_create(tick_cb, 200, NULL);
}
```
(Settings defaults are applied first, then `cfg` is derived from them once — no redundant reassignment.)

- [ ] **Step 3: Write `src/ui/ui.h` + `src/ui/ui.c` (neutral palette + softkey bar)**

`src/ui/ui.h`:
```c
#ifndef WILIDORO_UI_H
#define WILIDORO_UI_H
#include "lvgl.h"
/* neutral palette (Plan B2 themes override per-screen) */
#define UI_BG      0x0C0C12
#define UI_PANEL   0x141b26
#define UI_TEXT    0xDDE6F2
#define UI_MUTED   0x6B7C93
#define UI_ACCENT  0xFF5B45
#define UI_COOL    0x2DD4BF
/* Build a 5-cell softkey bar pinned to the screen bottom; labels[i]==NULL -> blank cell.
   Clicks invoke `cb(col)`. Returns the bar container; call ui_softkey_set_labels to relabel. */
typedef void (*ui_softkey_cb_t)(int col);
lv_obj_t *ui_softkey_bar(lv_obj_t *parent, ui_softkey_cb_t cb);
void      ui_softkey_set_labels(lv_obj_t *bar, const char *labels[5]);
lv_obj_t *ui_screen(void);   /* new full screen obj with neutral bg */
#endif
```
`src/ui/ui.c`:
```c
#include "ui.h"
static const uint32_t SK_COLORS[5] = {0x9AA6B2,0xE7C64B,0x39B36B,0x3B7DE0,0xD8503C};

static void sk_event(lv_event_t *e) {
    ui_softkey_cb_t cb = (ui_softkey_cb_t)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target_obj(e);
    int col = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (cb) cb(col);
}
lv_obj_t *ui_screen(void) {
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_pad_all(s, 0, 0);
    return s;
}
lv_obj_t *ui_softkey_bar(lv_obj_t *parent, ui_softkey_cb_t cb) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, 480, 34);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0D1420), 0);
    lv_obj_set_style_pad_all(bar, 2, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i < 5; i++) {
        lv_obj_t *btn = lv_button_create(bar);
        lv_obj_set_size(btn, 92, 28);
        lv_obj_set_style_bg_color(btn, lv_color_hex(SK_COLORS[i]), 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, sk_event, LV_EVENT_CLICKED, (void*)cb);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "");
        lv_obj_center(lbl);
    }
    return bar;
}
void ui_softkey_set_labels(lv_obj_t *bar, const char *labels[5]) {
    for (int i = 0; i < 5; i++) {
        lv_obj_t *btn = lv_obj_get_child(bar, i);
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        lv_label_set_text(lbl, labels[i] ? labels[i] : "");
        lv_obj_set_style_bg_opa(btn, labels[i] ? LV_OPA_COVER : LV_OPA_30, 0);
    }
}
```

- [ ] **Step 4: Write minimal screen stubs (fleshed out in Tasks 6–8)**

For each of `screen_timer`, `screen_settings`, `screen_nearby`, create `.h`:
```c
#ifndef WILIDORO_SCREEN_TIMER_H
#define WILIDORO_SCREEN_TIMER_H
#include "lvgl.h"
lv_obj_t *screen_timer_create(void);
void      screen_timer_update(void);
void      screen_timer_softkey(int col);
#endif
```
(and analogous `screen_settings.h`, `screen_nearby.h`). And a minimal `.c` for each that just makes a neutral screen with a title label so the app links and boots — e.g. `src/ui/screen_timer.c`:
```c
#include "screen_timer.h"
#include "ui.h"
#include "app.h"
#include "pomodoro.h"
static lv_obj_t *s_scr, *s_time;
lv_obj_t *screen_timer_create(void) {
    s_scr = ui_screen();
    s_time = lv_label_create(s_scr);
    lv_obj_set_style_text_color(s_time, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_48, 0);
    lv_obj_center(s_time);
    lv_label_set_text(s_time, "25:00");
    return s_scr;
}
void screen_timer_update(void) {
    uint32_t ms = pomodoro_remaining_ms(&app()->pomo, 0); /* replaced with hal now in Task 6 */
    (void)ms;
}
void screen_timer_softkey(int col) { (void)col; }
```
`screen_settings.c` and `screen_nearby.c`: same shape, a title label ("Settings" / "Nearby") centered, empty update/softkey. (Tasks 7–8 replace them.)

- [ ] **Step 5: Rewrite `src/target/main.c` to boot the app**

```c
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "hal.h"
#include "app.h"

int main(void) {
    board_init();
    st7796_init();
    st7796_fill_screen(0x0000);
    lvgl_port_init();
    ft6336_init();
    lvgl_port_register_touch();
    hal_init();               /* uartkbd + ws2812 + bl_pwm */
    app_init();               /* builds screens, starts tick */
    lv_timer_handler();       /* first frame */
    DIAG("wilidoro app up: sys=%u kHz\n", BOARD_SYS_CLOCK_KHZ);
    for (;;) {
        hal_pump();           /* drain uartkbd every iteration */
        lv_timer_handler();
        sleep_ms(5);
    }
}
```
Note: `bl_pwm_init()` now happens inside `hal_init()`, so it is no longer called in main — remove the old direct `bl_pwm` include/call. The blank-screen label code from Plan A Task 8 is fully replaced.

- [ ] **Step 6: Rewrite `src/sim/main.c` to boot the app**

```c
#define WILIDORO_SIM 1
#include "lvgl.h"
#include <SDL2/SDL.h>
#include "hal.h"
#include "app.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    lv_init();
    lv_sdl_window_create(480, 320);
    lv_sdl_mouse_create();
    hal_init();               /* SDL key watch */
    app_init();
    while (1) { hal_pump(); lv_timer_handler(); SDL_Delay(5); }
    return 0;
}
```

- [ ] **Step 7: Build both targets**

Run: `powershell -File tools/build.ps1 -Clean` (long timeout) → expect `build/wilidoro.uf2` links, no SRAM overflow.
Then configure+build the sim WITHOUT launching (run the cmake configure + `cmake --build build-sim`, not `tools/sim.ps1`) → expect `build-sim/wilidoro_sim.exe` links.
Paste both build tails + `ls -la build/wilidoro.uf2 build-sim/wilidoro_sim.exe`.

- [ ] **Step 8: Commit**

```bash
git add src/app src/ui src/target/main.c src/sim/main.c
git commit -m "feat(app): controller + screen scaffolding; both targets boot the app"
```

---

### Task 6: Timer face (neutral) — arc, MM:SS, state, dots, softkeys

**Files:**
- Modify: `src/ui/screen_timer.c`

**Interfaces:**
- Consumes: `ui.h`, `app.h`, `pomodoro.h`, `hal.h`.
- Produces: the working timer face + its softkey actions (Start/Pause/Resume/Skip/+5/Dismiss/Nearby/Menu).

- [ ] **Step 1: Replace `src/ui/screen_timer.c` with the full neutral face**

```c
#include "screen_timer.h"
#include "ui.h"
#include "app.h"
#include "hal.h"
#include "pomodoro.h"
#include <stdio.h>

static lv_obj_t *s_scr, *s_arc, *s_time, *s_state, *s_bar;

static void on_softkey(int col) { screen_timer_softkey(col); }

lv_obj_t *screen_timer_create(void) {
    s_scr = ui_screen();

    s_arc = lv_arc_create(s_scr);
    lv_obj_set_size(s_arc, 220, 220);
    lv_obj_align(s_arc, LV_ALIGN_CENTER, 0, -8);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, 1000);
    lv_arc_set_value(s_arc, 1000);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(UI_PANEL), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(UI_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);

    s_time = lv_label_create(s_scr);
    lv_obj_set_style_text_color(s_time, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_48, 0);
    lv_obj_align(s_time, LV_ALIGN_CENTER, 0, -18);
    lv_label_set_text(s_time, "25:00");

    s_state = lv_label_create(s_scr);
    lv_obj_set_style_text_color(s_state, lv_color_hex(UI_MUTED), 0);
    lv_obj_set_style_text_font(s_state, &lv_font_montserrat_16, 0);
    lv_obj_align(s_state, LV_ALIGN_CENTER, 0, 24);
    lv_label_set_text(s_state, "READY");

    s_bar = ui_softkey_bar(s_scr, on_softkey);
    screen_timer_update();
    return s_scr;
}

static void labels_for_state(pm_state_t st, bool alarm, const char *out[5]) {
    static const char *idle[5]  = {"Start", 0, 0, "Nearby", "Menu"};
    static const char *run[5]   = {"Pause", "Skip", "+5", "Nearby", "Menu"};
    static const char *paused[5]= {"Resume","Skip", "+5", "Nearby", "Menu"};
    static const char *alarm_l[5]={"Dismiss",0,0,0,"Menu"};
    const char **src = idle;
    if (alarm) src = alarm_l;
    else if (st == PM_FOCUS || st == PM_BREAK_SHORT || st == PM_BREAK_LONG) src = run;
    else if (st == PM_PAUSED) src = paused;
    for (int i=0;i<5;i++) out[i]=src[i];
}

void screen_timer_update(void) {
    app_t *a = app();
    uint32_t now = hal_now_ms();
    uint32_t rem = pomodoro_remaining_ms(&a->pomo, now);
    uint32_t total_ms = (uint32_t)a->pomo.cfg.focus_min * 60000u;
    if (a->pomo.state==PM_BREAK_SHORT) total_ms=(uint32_t)a->pomo.cfg.short_min*60000u;
    else if (a->pomo.state==PM_BREAK_LONG) total_ms=(uint32_t)a->pomo.cfg.long_min*60000u;

    char buf[8]; unsigned s = rem/1000; snprintf(buf, sizeof buf, "%02u:%02u", s/60, s%60);
    lv_label_set_text(s_time, buf);

    int32_t val = total_ms ? (int32_t)((uint64_t)rem*1000/total_ms) : 0;
    lv_arc_set_value(s_arc, val);
    bool break_phase = (a->pomo.state==PM_BREAK_SHORT||a->pomo.state==PM_BREAK_LONG);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(break_phase?UI_COOL:UI_ACCENT), LV_PART_INDICATOR);

    const char *name = a->alarm_active ? "TIME'S UP" :
        a->pomo.state==PM_FOCUS?"FOCUS":
        (break_phase?"BREAK":
        (a->pomo.state==PM_PAUSED?"PAUSED":"READY"));
    char st[24]; snprintf(st, sizeof st, "%s  %u/%u", name,
        (unsigned)(a->pomo.stats.completed % a->pomo.cfg.long_every),
        (unsigned)a->pomo.cfg.long_every);
    lv_label_set_text(s_state, st);

    const char *lbl[5]; labels_for_state(a->pomo.state, a->alarm_active, lbl);
    ui_softkey_set_labels(s_bar, lbl);
}

void screen_timer_softkey(int col) {
    app_t *a = app();
    uint32_t now = hal_now_ms();
    if (a->alarm_active) {
        if (col==0) { pomodoro_acknowledge(&a->pomo, now); a->alarm_active=false; }
        else if (col==4) app_goto(SCREEN_SETTINGS);
        return;
    }
    switch (a->pomo.state) {
        case PM_IDLE:
            if (col==0) pomodoro_start_focus(&a->pomo, now);
            else if (col==3) app_goto(SCREEN_NEARBY);
            else if (col==4) app_goto(SCREEN_SETTINGS);
            break;
        case PM_PAUSED:
            if (col==0) pomodoro_resume(&a->pomo, now);
            else if (col==1) pomodoro_skip(&a->pomo, now);
            else if (col==2) pomodoro_add5(&a->pomo, now);
            else if (col==3) app_goto(SCREEN_NEARBY);
            else if (col==4) app_goto(SCREEN_SETTINGS);
            break;
        default: /* FOCUS/BREAK */
            if (col==0) pomodoro_pause(&a->pomo, now);
            else if (col==1) pomodoro_skip(&a->pomo, now);
            else if (col==2) pomodoro_add5(&a->pomo, now);
            else if (col==3) app_goto(SCREEN_NEARBY);
            else if (col==4) app_goto(SCREEN_SETTINGS);
            break;
    }
}
```

- [ ] **Step 2: Build both targets**

Run: `powershell -File tools/build.ps1 -Clean` and the sim build (configure + `cmake --build build-sim`). Both must link cleanly. Paste tails + artifact listings.

- [ ] **Step 3: Commit**

```bash
git add src/ui/screen_timer.c
git commit -m "feat(ui): neutral timer face (arc + MM:SS + state + softkeys)"
```

---

### Task 7: Settings screen

**Files:**
- Modify: `src/ui/screen_settings.c`

**Interfaces:**
- Consumes: `ui.h`, `app.h`, `app_model.h`, `hal.h`.
- Produces: a scrollable settings list whose ± controls call the `app_settings_adjust_*` functions and whose Save applies config to the pomodoro; softkeys `Back · — · Default · — · Save`.

- [ ] **Step 1: Replace `src/ui/screen_settings.c`**

```c
#include "screen_settings.h"
#include "ui.h"
#include "app.h"
#include "app_model.h"
#include <stdio.h>

static lv_obj_t *s_scr, *s_list, *s_bar;
static lv_obj_t *s_val_focus, *s_val_short, *s_val_long, *s_val_vol, *s_val_beacon, *s_val_theme;

static void refresh_values(void) {
    app_settings_t *s = &app()->settings; char b[16];
    snprintf(b,sizeof b,"%u min",s->focus_min); lv_label_set_text(s_val_focus,b);
    snprintf(b,sizeof b,"%u min",s->short_min); lv_label_set_text(s_val_short,b);
    snprintf(b,sizeof b,"%u min",s->long_min);  lv_label_set_text(s_val_long,b);
    snprintf(b,sizeof b,"%u%%",s->volume);      lv_label_set_text(s_val_vol,b);
    lv_label_set_text(s_val_beacon, s->beacon_on?"on":"off");
    static const char *tn[3]={"Neon Arc","Arcade","Flip Clock"};
    lv_label_set_text(s_val_theme, tn[s->theme%3]);
}

/* Each row: [label] [-] [value] [+]. user_data on +/- encodes which setting & sign. */
enum { SET_FOCUS=1, SET_SHORT, SET_LONG, SET_VOL, SET_BEACON, SET_THEME };
static void adj_event(lv_event_t *e) {
    intptr_t code = (intptr_t)lv_event_get_user_data(e);
    int which = (int)(code >> 1); int sign = (code & 1) ? +1 : -1;
    app_settings_t *s = &app()->settings;
    switch (which) {
        case SET_FOCUS: app_settings_adjust_focus(s, sign); break;
        case SET_SHORT: app_settings_adjust_short(s, sign); break;
        case SET_LONG:  app_settings_adjust_long(s, sign);  break;
        case SET_VOL:   app_settings_adjust_volume(s, sign);break;
        case SET_BEACON:s->beacon_on = !s->beacon_on; break;
        case SET_THEME: app_settings_cycle_theme(s); break;
    }
    refresh_values();
}

static lv_obj_t *add_row(const char *name, int which) {
    lv_obj_t *row = lv_obj_create(s_list);
    lv_obj_set_size(row, 460, 40);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_PANEL), 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *l = lv_label_create(row); lv_label_set_text(l,name);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_TEXT), 0); lv_obj_set_width(l, 230);
    lv_obj_t *minus = lv_button_create(row); lv_obj_set_size(minus,34,30);
    lv_obj_add_event_cb(minus, adj_event, LV_EVENT_CLICKED, (void*)(intptr_t)((which<<1)|0));
    lv_obj_t *ml=lv_label_create(minus); lv_label_set_text(ml,"-"); lv_obj_center(ml);
    lv_obj_t *val = lv_label_create(row); lv_obj_set_width(val, 90);
    lv_obj_set_style_text_color(val, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *plus = lv_button_create(row); lv_obj_set_size(plus,34,30);
    lv_obj_add_event_cb(plus, adj_event, LV_EVENT_CLICKED, (void*)(intptr_t)((which<<1)|1));
    lv_obj_t *pl=lv_label_create(plus); lv_label_set_text(pl,"+"); lv_obj_center(pl);
    return val;
}

lv_obj_t *screen_settings_create(void) {
    s_scr = ui_screen();
    lv_obj_t *title = lv_label_create(s_scr); lv_label_set_text(title,"SETTINGS");
    lv_obj_set_style_text_color(title, lv_color_hex(UI_MUTED),0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);

    s_list = lv_obj_create(s_scr);
    lv_obj_set_size(s_list, 476, 236);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(UI_BG), 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);

    s_val_focus  = add_row("Focus length", SET_FOCUS);
    s_val_short  = add_row("Short break",  SET_SHORT);
    s_val_long   = add_row("Long break",   SET_LONG);
    s_val_vol    = add_row("Volume",       SET_VOL);
    s_val_beacon = add_row("Beacon",       SET_BEACON);
    s_val_theme  = add_row("Theme",        SET_THEME);

    s_bar = ui_softkey_bar(s_scr, screen_settings_softkey);
    const char *lbl[5] = {"Back", 0, "Default", 0, "Save"};
    ui_softkey_set_labels(s_bar, lbl);
    refresh_values();
    return s_scr;
}

void screen_settings_update(void) { /* values refresh on edit; nothing periodic */ }

void screen_settings_softkey(int col) {
    app_t *a = app();
    if (col==0) { app_goto(SCREEN_TIMER); }
    else if (col==2) { app_settings_defaults(&a->settings); refresh_values(); }
    else if (col==4) {
        pm_config_t c = { a->settings.focus_min, a->settings.short_min, a->settings.long_min, a->settings.long_every };
        if (a->pomo.state == PM_IDLE) pomodoro_init(&a->pomo, c); /* apply only when idle to avoid mid-session surprise */
        app_goto(SCREEN_TIMER);
    }
}
```
Note the softkey bar's `cb` is `screen_settings_softkey` directly (its signature matches `ui_softkey_cb_t`). Confirm `lv_obj_set_style_text_align` and `lv_obj_remove_flag`/`LV_OBJ_FLAG_CLICKABLE` exist in 9.2.2 (they do); if a specific inline style setter name differs, grep `third_party/lvgl/src/core/lv_obj_style_gen.h`.

- [ ] **Step 2: Build both targets** (as in Task 6 Step 2). Paste tails.

- [ ] **Step 3: Commit**

```bash
git add src/ui/screen_settings.c
git commit -m "feat(ui): settings screen wired to app_settings adjusters"
```

---

### Task 8: Nearby screen

**Files:**
- Modify: `src/ui/screen_nearby.c`

**Interfaces:**
- Consumes: `ui.h`, `app.h`, `app_model.h`.
- Produces: a list rebuilt each update from the neighbor table (name, state, minutes, count); softkey `Back`.

- [ ] **Step 1: Replace `src/ui/screen_nearby.c`**

```c
#include "screen_nearby.h"
#include "ui.h"
#include "app.h"
#include "app_model.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_scr, *s_list, *s_bar, *s_empty;

lv_obj_t *screen_nearby_create(void) {
    s_scr = ui_screen();
    lv_obj_t *title = lv_label_create(s_scr); lv_label_set_text(title,"NEARBY");
    lv_obj_set_style_text_color(title, lv_color_hex(UI_MUTED),0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);

    s_list = lv_obj_create(s_scr);
    lv_obj_set_size(s_list, 476, 236);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(UI_BG), 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);

    s_empty = lv_label_create(s_scr);
    lv_obj_set_style_text_color(s_empty, lv_color_hex(UI_MUTED), 0);
    lv_label_set_text(s_empty, "no devices heard");
    lv_obj_center(s_empty);

    s_bar = ui_softkey_bar(s_scr, screen_nearby_softkey);
    const char *lbl[5] = {"Back",0,0,0,0};
    ui_softkey_set_labels(s_bar, lbl);
    return s_scr;
}

void screen_nearby_update(void) {
    /* rebuild the list from the neighbor table each tick */
    lv_obj_clean(s_list);
    neighbor_table_t *t = &app()->neighbors;
    int shown = 0;
    for (int i = 0; i < NEIGHBOR_MAX; i++) {
        if (!t->items[i].used) continue;
        neighbor_t *n = &t->items[i];
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_set_size(row, 460, 44);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_PANEL), 0);
        char nm[APP_NAME_LEN+1]; memcpy(nm, n->name, APP_NAME_LEN); nm[APP_NAME_LEN]=0;
        for (int k=APP_NAME_LEN-1;k>=0 && nm[k]==' ';k--) nm[k]=0;
        lv_obj_t *name = lv_label_create(row); lv_label_set_text(name, nm);
        lv_obj_set_style_text_color(name, lv_color_hex(UI_TEXT), 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 6, 4);
        const char *stname = n->state==BST_FOCUS?"focusing":(n->state==BST_BREAK?"on break":"idle");
        char sub[40];
        if (n->state==BST_IDLE) snprintf(sub,sizeof sub,"idle - %u done today", n->completed);
        else snprintf(sub,sizeof sub,"%s - %u min left", stname, n->minutes_left);
        lv_obj_t *s = lv_label_create(row); lv_label_set_text(s, sub);
        lv_obj_set_style_text_color(s, lv_color_hex(UI_MUTED), 0);
        lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 6, -4);
        shown++;
    }
    lv_obj_set_style_bg_opa(s_empty, shown?LV_OPA_TRANSP:LV_OPA_COVER, 0);
    lv_obj_add_flag(s_empty, shown?LV_OBJ_FLAG_HIDDEN:0);
    if (!shown) lv_obj_remove_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
}

void screen_nearby_softkey(int col) { if (col==0) app_goto(SCREEN_TIMER); }
```
Note: rebuilding the list every 200ms tick with `lv_obj_clean` is simple and fine for ≤8 rows; if it flickers in practice, Plan B2/C can switch to diffing. Confirm `lv_obj_clean`, `lv_obj_add_flag`/`lv_obj_remove_flag`, and `LV_OBJ_FLAG_HIDDEN` exist in 9.2.2 (they do).

- [ ] **Step 2: Build both targets** (as before). Paste tails.

- [ ] **Step 3: Commit**

```bash
git add src/ui/screen_nearby.c
git commit -m "feat(ui): nearby screen listing beacon neighbors"
```

---

### Task 9: Integration pass + sim launch smoke + user checkpoint

**Files:**
- Modify: as needed for fixes surfaced during integration (small).

**Interfaces:** none new — this task verifies the whole app coheres.

- [ ] **Step 1: Full clean build of both targets**

Run `powershell -File tools/build.ps1 -Clean` and the sim configure+build. Both link cleanly, no warnings, no SRAM overflow. Paste tails + artifacts.

- [ ] **Step 2: Sim launch smoke test (no GUI assertion)**

Launch `build-sim/wilidoro_sim.exe` in the background for ~4 seconds, confirm it does not crash/exit early (check the process ran and was still alive, then terminate it). Paste the result. (Do NOT attempt to assert on-screen pixels — visual confirmation is the user checkpoint below.)

- [ ] **Step 3: Re-run the whole host suite**

Run `powershell -File tools/test.ps1` → confirm all test binaries pass (now includes `test_app_model`). Paste the summary.

- [ ] **Step 4: Prepare the user checkpoint note**

In the report, write a short "how to verify" for the user: run `tools/sim.ps1`; keys `Z X C V B` = the 5 softkeys (grey…red), arrows + Enter = D-pad; walk Start → Pause/Resume → Skip → +5 → Menu(Settings) → adjust → Save → Nearby (a fake "JEN" neighbor appears) → Back. And note that on-device flashing is a separate user-gated step.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "chore(app): Plan B1 integration pass — both targets build, sim smoke passes"
```

---

## Self-Review

**Spec coverage (Plan B1 scope):** Timer state machine wired to UI (Tasks 5–6), the three screens with softkey + touch navigation (Tasks 6–8), Settings editing durations/volume/tick/beacon/name/theme-placeholder (Task 7), Nearby list from beacon neighbors (Task 8), session-only stats shown on the timer face (Task 6), and the real FreeWili button model (Task 1). Deferred by design to Plan B2 (theme visuals) and Plan C (LED patterns, audio, gestures, auto-dim, real radio) — stated in Global Constraints and Scope note. The Settings `name` field is stored but its on-device text-entry UI (chord keyboard) is out of scope for B1; it defaults to "WILI" and is editable in a later pass — noted so it isn't mistaken for a gap.

**Placeholder scan:** No TBD/TODO. Pure-logic tasks (1,2) carry full test + impl code. UI tasks carry complete widget-construction code using verified LVGL 9.2.2 signatures. The screen stubs in Task 5 are explicitly minimal-but-complete and are replaced in Tasks 6–8.

**Type consistency:** `screen_X_create/update/softkey` signatures are identical across `app.c`, the headers (Task 5), and the implementations (Tasks 6–8). `ui_softkey_cb_t` = `void(*)(int)` matches every screen's `screen_X_softkey`. `hal_btn_t` order (grey=0…) matches the softkey column indices used in `route_softkey`/`screen_*_softkey` and the device `uartkbd_btn_t` cast. `app_settings_adjust_*` names match between Task 1 (def), the tests, and Task 7 (callers).

**Known integration risks the implementer must resolve against real headers (called out inline in the tasks):** exact wilibsp `uartkbd` include path and enum order (Task 4); a few LVGL inline style/flag setter names (`lv_obj_set_style_pad_all`, `lv_obj_set_style_text_align`, `lv_obj_remove_flag`, `lv_obj_get_child`, `lv_obj_set_user_data`/`lv_obj_get_user_data`) — all standard 9.2.2 but grep to confirm exact spelling.

## Roadmap after B1

- **Plan B2 — Themes:** extract a `theme_t` interface (colors, fonts, timer-face `build`/`update`, LED pattern hook, sound set), refactor `screen_timer` to delegate to the active theme, implement Neon Arc / Tomato Arcade / Flip Clock, and activate the Settings "Theme" row (already wired to `app_settings_cycle_theme`).
- **Plan C — Hardware features:** replace the device HAL stubs with real drivers — LED theme patterns + celebrations, synthesized audio (start/end/tick/blip), BMI323 gestures → `core/gestures` → app actions, OPT4001 → `core/dimming` → `hal_backlight`, and CC1101 beacon TX (`beacon_ook_encode`→`ook_tx_send`) + RX (`gdo_capture`→`beacon_ook_decode`) populating the neighbor table for real.
